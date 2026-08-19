/* $Id: VBoxDX.cpp 115080 2026-08-19 11:45:15Z vitali.pelenjow@oracle.com $ */
/** @file
 * VirtualBox D3D user mode driver.
 */

/*
 * Copyright (C) 2022-2025 Oracle and/or its affiliates.
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

#include <iprt/alloc.h>
#include <iprt/errcore.h>
#include <iprt/thread.h>
#include <VBox/log.h>
#include <VBox/cdefs.h> /* VBOX_STRICT */

#include <iprt/win/windows.h>
#include <iprt/win/d3dkmthk.h>

#include <d3d10umddi.h>

#include "VBoxDX.h"
#include "VBoxDXCmd.h"

#pragma pack(1) /* VMSVGA structures are '__packed'. */
/* Declares a static array. */
#include <svga3d_surfacedefs.h>
#pragma pack()

static HRESULT vboxdxQueryGetDataInternal(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, VOID* pData, UINT DataSize);
static void dxProcessPendingOffers(PVBOXDX_DEVICE pDevice);

static uint32_t vboxDXGetSubresourceOffset(VBOXDXALLOCATIONDESC const *pAllocationDesc, UINT Subresource)
{
    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pAllocationDesc->surfaceInfo.size.width;
    baseLevelSize.height = pAllocationDesc->surfaceInfo.size.height;
    baseLevelSize.depth  = pAllocationDesc->surfaceInfo.size.depth;

    uint32_t const numMipLevels = pAllocationDesc->surfaceInfo.numMipLevels;
    uint32_t const face = Subresource / numMipLevels;
    uint32_t const mip = Subresource % numMipLevels;
    return svga3dsurface_get_image_offset(pAllocationDesc->surfaceInfo.format,
                                          baseLevelSize, numMipLevels, face, mip);
}


DECLINLINE(uint32_t) dxClampedMul(uint32_t a, uint32_t b)
{
    uint64_t const u64Tmp = (uint64_t)a * (uint64_t)b;
    if (RT_LIKELY(u64Tmp <= UINT64_C(0xFFFFFFFF)))
        return (uint32_t)u64Tmp;
    return UINT32_C(0xFFFFFFFF);
}


DECLINLINE(uint32_t) dxClampedAddDiv(uint32_t a, uint32_t b, uint32_t d)
{
    uint64_t const u64Tmp = (uint64_t)a + (uint64_t)b;
    if (RT_LIKELY(u64Tmp <= UINT64_C(0xFFFFFFFF) && d > 0))
        return u64Tmp / d;
    return UINT32_C(0xFFFFFFFF);
}


DECLINLINE(const struct svga3d_surface_desc *)vboxDXGetSurfaceDesc(SVGA3dSurfaceFormat format)
{
    AssertReturn(format < RT_ELEMENTS(svga3d_surface_descs), &svga3d_surface_descs[SVGA3D_FORMAT_INVALID]);
    return &svga3d_surface_descs[format];
}


/* Description of a tightly packed box. */
typedef struct VBOXDXRESOURCEBOXDESC
{
    uint32_t cxBlocks;     /* Width in blocks. */
    uint32_t cyBlocks;     /* Height in blocks. */
    uint32_t czBlocks;     /* Depth in blocks. */
    uint32_t cbRowPitch;   /* Scanline size for non-planar formats, 0 for planar formats. */
    uint32_t cbDepthPitch; /* Slice size for non-planar formats, 0 for planar formats. */
    uint32_t cbBox;        /* Size of the box in bytes. */
} VBOXDXRESOURCEBOXDESC;


static void vboxDXGetResourceBoxDesc(VBOXDXALLOCATIONDESC const *pAllocationDesc, const SVGA3dBox &box,
                                     VBOXDXRESOURCEBOXDESC *pBoxDesc)
{
    const struct svga3d_surface_desc *desc = vboxDXGetSurfaceDesc(pAllocationDesc->surfaceInfo.format);

    /* Size in blocks. */
    pBoxDesc->cxBlocks = dxClampedAddDiv(box.w, desc->block_size.width - 1, desc->block_size.width);
    pBoxDesc->cyBlocks = dxClampedAddDiv(box.h, desc->block_size.height - 1, desc->block_size.height);
    pBoxDesc->czBlocks = dxClampedAddDiv(box.d, desc->block_size.depth - 1, desc->block_size.depth);

    if (RT_LIKELY((desc->block_desc & SVGA3DBLOCKDESC_PLANAR_YUV) == 0))
    {
        pBoxDesc->cbRowPitch = dxClampedMul(pBoxDesc->cxBlocks, desc->pitch_bytes_per_block);
        pBoxDesc->cbDepthPitch = dxClampedMul(pBoxDesc->cbRowPitch, pBoxDesc->cyBlocks);
        pBoxDesc->cbBox = dxClampedMul(dxClampedMul(pBoxDesc->cbRowPitch, pBoxDesc->cyBlocks),
                                       pBoxDesc->czBlocks);
        return;
    }

    /* Planar format. */
    pBoxDesc->cbRowPitch = 0;
    pBoxDesc->cbDepthPitch = 0;
    pBoxDesc->cbBox = dxClampedMul(dxClampedMul(dxClampedMul(pBoxDesc->cxBlocks, pBoxDesc->cyBlocks),
                                                             pBoxDesc->czBlocks),
                                                             desc->bytes_per_block);
}


static uint32_t vboxDXGetSubresourceSize(VBOXDXALLOCATIONDESC const *pAllocationDesc, UINT Subresource)
{
    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pAllocationDesc->surfaceInfo.size.width;
    baseLevelSize.height = pAllocationDesc->surfaceInfo.size.height;
    baseLevelSize.depth  = pAllocationDesc->surfaceInfo.size.depth;

    uint32_t const numMipLevels = pAllocationDesc->surfaceInfo.numMipLevels;
    /*uint32_t const face = Subresource / numMipLevels; - unused */
    uint32_t const mip = Subresource % numMipLevels;

    const struct svga3d_surface_desc *desc = svga3dsurface_get_desc(pAllocationDesc->surfaceInfo.format);

    surf_size_struct mipSize = svga3dsurface_get_mip_size(baseLevelSize, mip);
    return svga3dsurface_get_image_buffer_size(desc, &mipSize, 0);
}


static void vboxDXGetSubresourcePitch(VBOXDXALLOCATIONDESC const *pAllocationDesc, UINT Subresource, UINT *pRowPitch, UINT *pDepthPitch)
{
    if (pAllocationDesc->surfaceInfo.format == SVGA3D_BUFFER)
    {
        *pRowPitch = pAllocationDesc->surfaceInfo.size.width;
        *pDepthPitch = *pRowPitch;
        return;
    }

    uint32_t const numMipLevels = pAllocationDesc->surfaceInfo.numMipLevels;
    /* uint32_t const face = Subresource / numMipLevels; - unused */
    uint32_t const mip = Subresource % numMipLevels;

    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pAllocationDesc->surfaceInfo.size.width;
    baseLevelSize.height = pAllocationDesc->surfaceInfo.size.height;
    baseLevelSize.depth  = pAllocationDesc->surfaceInfo.size.depth;

    const struct svga3d_surface_desc *desc = svga3dsurface_get_desc(pAllocationDesc->surfaceInfo.format);

    surf_size_struct mipSize = svga3dsurface_get_mip_size(baseLevelSize, mip);
    surf_size_struct blocks;
    svga3dsurface_get_size_in_blocks(desc, &mipSize, &blocks);

    *pRowPitch = blocks.width * desc->pitch_bytes_per_block;
    *pDepthPitch = blocks.height * (*pRowPitch);
}


static void vboxDXGetResourceBoxDimensions(VBOXDXALLOCATIONDESC const *pAllocationDesc, UINT Subresource, SVGA3dBox *pBox,
                                           uint32_t *pOffPixel, uint32_t *pcbRow, uint32_t *pcRows, uint32_t *pDepth)
{
    if (pAllocationDesc->surfaceInfo.format == SVGA3D_BUFFER)
    {
        *pOffPixel = pBox->x;
        *pcbRow = pBox->w;
        *pcRows = 1;
        *pDepth = 1;
        return;
    }

    const struct svga3d_surface_desc *desc = svga3dsurface_get_desc(pAllocationDesc->surfaceInfo.format);

    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pAllocationDesc->surfaceInfo.size.width;
    baseLevelSize.height = pAllocationDesc->surfaceInfo.size.height;
    baseLevelSize.depth  = pAllocationDesc->surfaceInfo.size.depth;

    uint32_t const numMipLevels = pAllocationDesc->surfaceInfo.numMipLevels;
    uint32_t const mip = Subresource % numMipLevels;

    surf_size_struct mipSize = svga3dsurface_get_mip_size(baseLevelSize, mip);

    surf_size_struct boxSize;
    boxSize.width = pBox->w;
    boxSize.height = pBox->h;
    boxSize.depth = pBox->d;
    surf_size_struct blocks;
    svga3dsurface_get_size_in_blocks(desc, &boxSize, &blocks);

    *pOffPixel = svga3dsurface_get_pixel_offset(pAllocationDesc->surfaceInfo.format,
                                                mipSize.width, mipSize.height,
                                                pBox->x, pBox->y, pBox->z);
    *pcbRow = blocks.width * desc->pitch_bytes_per_block;
    *pcRows = blocks.height;
    *pDepth = pBox->d;
}


static void vboxDXGetSubresourceBox(VBOXDXALLOCATIONDESC const *pAllocationDesc, UINT Subresource, SVGA3dBox *pBox)
{
    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pAllocationDesc->surfaceInfo.size.width;
    baseLevelSize.height = pAllocationDesc->surfaceInfo.size.height;
    baseLevelSize.depth  = pAllocationDesc->surfaceInfo.size.depth;

    uint32_t const numMipLevels = pAllocationDesc->surfaceInfo.numMipLevels;
    uint32_t const mip = Subresource % numMipLevels;

    surf_size_struct mipSize = svga3dsurface_get_mip_size(baseLevelSize, mip);

    pBox->x = 0;
    pBox->y = 0;
    pBox->z = 0;
    pBox->w = mipSize.width;
    pBox->h = mipSize.height;
    pBox->d = mipSize.depth;
}


static VBOXDXUPLOADBATCH *dxUploadBatchAllocate(PVBOXDX_DEVICE pDevice)
{
    VBOXDXUPLOADBATCH *pBatch = (VBOXDXUPLOADBATCH *)RTMemAllocZ(sizeof(VBOXDXUPLOADBATCH));
    AssertReturnStmt(pBatch, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), NULL);

    vboxDXCreateQuery(pDevice, &pBatch->queryBatchCompleted, D3D10DDI_QUERY_EVENT, 0);
    return pBatch;
}


static void dxUploadBatchDeallocate(PVBOXDX_DEVICE pDevice, VBOXDXUPLOADBATCH *pBatch)
{
    vboxDXDestroyQuery(pDevice, &pBatch->queryBatchCompleted);
    RTMemFree(pBatch);
}


static VBOXDXUPLOADBATCH *dxUploadBatchGet(PVBOXDX_DEVICE pDevice)
{
    VBOXDXUPLOADBATCH *pBatch = RTListRemoveFirst(&pDevice->upload.listLookasideBatches,
                                                  VBOXDXUPLOADBATCH, nodeUploadBatch);
    if (!pBatch)
        pBatch = dxUploadBatchAllocate(pDevice);
    else
        pBatch->cbBatch = 0;

    return pBatch;
}


static void dxUploadBatchRetire(PVBOXDX_DEVICE pDevice, VBOXDXUPLOADBATCH *pBatch)
{
    RTListNodeRemove(&pBatch->nodeUploadBatch);
    RTListAppend(&pDevice->upload.listLookasideBatches, &pBatch->nodeUploadBatch);
}


static VBOXDXTRANSIENTHEAPBLOCKDESC *dxTransientBlockAllocate(VBOXDXTRANSIENTHEAP *pHeap, uint32_t offBlock, uint32_t cbBlock)
{
    /** @todo Allocate block descriptors in chunks in order to reduce amount of heap allocations. */
    VBOXDXTRANSIENTHEAPBLOCKDESC *pBlock = (VBOXDXTRANSIENTHEAPBLOCKDESC *)RTMemAlloc(sizeof(VBOXDXTRANSIENTHEAPBLOCKDESC));
    AssertReturnStmt(pBlock, vboxDXDeviceSetError(pHeap->pDevice, E_OUTOFMEMORY), NULL);

    pBlock->pHeap = pHeap;
    RT_ZERO(pBlock->nodeTransientHeap);
    RT_ZERO(pBlock->nodeTransientBlock);
    pBlock->offBlock = offBlock;
    pBlock->cbBlock = cbBlock;
    pBlock->fFreeBlock = 1;
    pBlock->fReserved = 0;
    return pBlock;
}


static void dxTransientBlockDeallocate(VBOXDXTRANSIENTHEAPBLOCKDESC *pBlock)
{
    Assert(pBlock->nodeTransientHeap.pNext == NULL);
    Assert(pBlock->nodeTransientBlock.pNext == NULL);
    Assert(pBlock->fFreeBlock);
    RTMemFree(pBlock);
}


static VBOXDXTRANSIENTHEAPBATCH *dxTransientHeapBatchAllocate(VBOXDXTRANSIENTHEAP *pHeap)
{
    VBOXDXTRANSIENTHEAPBATCH *pBatch = (VBOXDXTRANSIENTHEAPBATCH *)RTMemAlloc(sizeof(VBOXDXTRANSIENTHEAPBATCH));
    AssertReturnStmt(pBatch, vboxDXDeviceSetError(pHeap->pDevice, E_OUTOFMEMORY), NULL);

    pBatch->pHeap = pHeap;
    RT_ZERO(pBatch->nodeTransientBatch);
    RTListInit(&pBatch->listPendingBlocks);
    vboxDXCreateQuery(pHeap->pDevice, &pBatch->queryBatchCompleted, D3D10DDI_QUERY_EVENT, 0);
    return pBatch;
}


static void dxTransientHeapBatchDeallocate(VBOXDXTRANSIENTHEAPBATCH *pBatch)
{
    Assert(pBatch->nodeTransientBatch.pNext == NULL);
    Assert(RTListIsEmpty(&pBatch->listPendingBlocks));
    vboxDXDestroyQuery(pBatch->pHeap->pDevice, &pBatch->queryBatchCompleted);
    RTMemFree(pBatch);
}


static VBOXDXTRANSIENTHEAPBATCH *dxTransientHeapBatchGet(VBOXDXTRANSIENTHEAP *pHeap)
{
    VBOXDXTRANSIENTHEAPBATCH *pBatch = RTListRemoveFirst(&pHeap->listLookasideBatches,
                                                         VBOXDXTRANSIENTHEAPBATCH, nodeTransientBatch);
    if (!pBatch)
        pBatch = dxTransientHeapBatchAllocate(pHeap);
    else
        Assert(RTListIsEmpty(&pBatch->listPendingBlocks));

    return pBatch;
}


static void dxTransientHeapBatchRetire(VBOXDXTRANSIENTHEAPBATCH *pBatch)
{
    Assert(RTListIsEmpty(&pBatch->listPendingBlocks));
    RTListNodeRemove(&pBatch->nodeTransientBatch);
    RTListAppend(&pBatch->pHeap->listLookasideBatches, &pBatch->nodeTransientBatch);
}


static DECLCALLBACK(int) dxReclaimFreeBlocksCb(PAVLU32NODECORE pNode, void *pvUser)
{
    VBOXDXTRANSIENTHEAPFREEBLOCKS *p = (VBOXDXTRANSIENTHEAPFREEBLOCKS *)pNode;
    VBOXDXTRANSIENTHEAP *pHeap = (VBOXDXTRANSIENTHEAP *)pvUser;

    VBOXDXTRANSIENTHEAPBLOCKDESC *pIter, *pNext;
    RTListForEachSafe(&p->listFreeBlocks, pIter, pNext, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientBlock)
    {
        Assert(pIter->fFreeBlock);

        RTListNodeRemove(&pIter->nodeTransientBlock);
        RTListAppend(&pHeap->listFreeBlocks, &pIter->nodeTransientBlock);
    }

    return 0;
}


static void dxTransientHeapMergeFreeBlocks(VBOXDXTRANSIENTHEAP *pHeap)
{
    /* First, move free blocks from per-size lists to the generic list. */
    RTAvlU32DoWithAll(&pHeap->treeFreeBlocks, 0, dxReclaimFreeBlocksCb, pHeap);

    /* Merge free blocks in the generic list. */
    VBOXDXTRANSIENTHEAPBLOCKDESC *pIter, *pNext;
    RTListForEachSafe(&pHeap->listFreeBlocks, pIter, pNext, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientBlock)
    {
        Assert(pIter->fFreeBlock);

        /* Check if this block can be merged with the previous block in the heap list. */
        VBOXDXTRANSIENTHEAPBLOCKDESC *pPrevBlock = RTListNodeGetPrev(&pIter->nodeTransientHeap, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientHeap);
        if (   !RTListNodeIsDummy(&pHeap->listBlocks, pPrevBlock, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientHeap)
            && pPrevBlock->fFreeBlock)
        {
            /* The block can be merged with the previous block. */
            Assert(pPrevBlock->offBlock + pPrevBlock->cbBlock == pIter->offBlock);

            RTListNodeRemove(&pIter->nodeTransientBlock);
            RTListNodeRemove(&pIter->nodeTransientHeap);

            pPrevBlock->cbBlock += pIter->cbBlock;
            dxTransientBlockDeallocate(pIter);
        }
    }

#ifdef DEBUG
    /* Check that all block were merged. */
    RTListForEach(&pHeap->listFreeBlocks, pIter, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientBlock)
    {
        Assert(pIter->fFreeBlock);

        VBOXDXTRANSIENTHEAPBLOCKDESC *pPrevBlock = RTListNodeGetPrev(&pIter->nodeTransientHeap, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientHeap);
        if (   !RTListNodeIsDummy(&pHeap->listBlocks, pPrevBlock, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientHeap)
            && pPrevBlock->fFreeBlock)
        {
            AssertFailed();
        }

        VBOXDXTRANSIENTHEAPBLOCKDESC *pNextBlock = RTListNodeGetNext(&pIter->nodeTransientHeap, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientHeap);
        if (   !RTListNodeIsDummy(&pHeap->listBlocks, pNextBlock, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientHeap)
            && pNextBlock->fFreeBlock)
        {
            AssertFailed();
        }
    }
#endif /* DEBUG */
}


static VBOXDXTRANSIENTHEAPBLOCKDESC *dxTransientBlockReserve(VBOXDXTRANSIENTHEAP *pHeap, uint32_t cbBlock)
{
    VBOXDXTRANSIENTHEAPFREEBLOCKS *pFreeBlocks = (VBOXDXTRANSIENTHEAPFREEBLOCKS *)RTAvlU32Get(&pHeap->treeFreeBlocks, cbBlock);
    VBOXDXTRANSIENTHEAPBLOCKDESC *pBlock = pFreeBlocks
                                         ? RTListRemoveFirst(&pFreeBlocks->listFreeBlocks, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientBlock)
                                         : NULL;
    if (pBlock)
    {
        Assert(pBlock->fFreeBlock);
        pBlock->fFreeBlock = 0;
        return pBlock;
    }

    /* Walk the free list for a matching block. */
    VBOXDXTRANSIENTHEAPBLOCKDESC *pIter;
    RTListForEach(&pHeap->listFreeBlocks, pIter, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientBlock)
    {
        Assert(pIter->fFreeBlock);
        if (pIter->cbBlock >= cbBlock)
        {
            pBlock = pIter;
            break;
        }
    }

    if (RT_LIKELY(pBlock))
    { /* likely */ }
    else
    {
        /* There were no space for the block in both generic and per-size lists.
         * Move the free blocks from per-size lists to the generic free list and merge the free blocks.
         */
        dxTransientHeapMergeFreeBlocks(pHeap);

        /* Repeat the search for a free block. */
        RTListForEach(&pHeap->listFreeBlocks, pIter, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientBlock)
        {
            Assert(pIter->fFreeBlock);
            if (pIter->cbBlock >= cbBlock)
            {
                pBlock = pIter;
                break;
            }
        }

        if (!pBlock)
            return NULL;
    }

    Assert(pBlock);
    if (pBlock->cbBlock > cbBlock)
    {
        /* Split the block and return the newly allocated block. */
        VBOXDXTRANSIENTHEAPBLOCKDESC *pNewBlock = dxTransientBlockAllocate(pHeap, pBlock->offBlock, cbBlock);
        AssertReturn(pNewBlock, NULL);

        pBlock->offBlock += cbBlock;
        pBlock->cbBlock -= cbBlock;

        /* Insert the new block in the sorted list of all blocks. */
        RTListNodeInsertBefore(&pBlock->nodeTransientHeap, &pNewBlock->nodeTransientHeap);

        pBlock = pNewBlock;
    }
    else
    {
        /* This can happen when the never allocated free block has the requested size coincidentally. */
        RTListNodeRemove(&pBlock->nodeTransientBlock); /* Remove from listFreeBlocks */
    }

    pBlock->fFreeBlock = 0;
    return pBlock;
}


static void dxTransientBlockRelease(VBOXDXTRANSIENTHEAPBLOCKDESC *pBlock)
{
    Assert(!pBlock->fFreeBlock);
    Assert(pBlock->nodeTransientBlock.pNext == NULL);

    VBOXDXTRANSIENTHEAPFREEBLOCKS *pFreeBlocks = (VBOXDXTRANSIENTHEAPFREEBLOCKS *)RTAvlU32Get(&pBlock->pHeap->treeFreeBlocks, pBlock->cbBlock);
    if (!pFreeBlocks)
    {
        pFreeBlocks = (VBOXDXTRANSIENTHEAPFREEBLOCKS *)RTMemAllocZ(sizeof(VBOXDXTRANSIENTHEAPFREEBLOCKS));
        AssertReturnVoidStmt(pFreeBlocks, vboxDXDeviceSetError(pBlock->pHeap->pDevice, E_OUTOFMEMORY));

        pFreeBlocks->Core.Key = pBlock->cbBlock;
        RTListInit(&pFreeBlocks->listFreeBlocks);

        bool fInserted = RTAvlU32Insert(&pBlock->pHeap->treeFreeBlocks, &pFreeBlocks->Core);
        AssertReturnVoidStmt(fInserted, RTMemFree(pFreeBlocks); vboxDXDeviceSetError(pBlock->pHeap->pDevice, E_OUTOFMEMORY));
    }

    RTListPrepend(&pFreeBlocks->listFreeBlocks, &pBlock->nodeTransientBlock);
    pBlock->fFreeBlock = 1;

}


static void dxTransientHeapOnFlush(VBOXDXTRANSIENTHEAP *pHeap)
{
    /* Check which batches have been processed by the host. */
    VBOXDXTRANSIENTHEAPBATCH *pBatch, *pNext;
    RTListForEachSafe(&pHeap->listTransientBatches, pBatch, pNext, VBOXDXTRANSIENTHEAPBATCH, nodeTransientBatch)
    {
        BOOL fCompleted = FALSE;
        HRESULT hr = vboxdxQueryGetDataInternal(pHeap->pDevice, &pBatch->queryBatchCompleted,
                                                &fCompleted, sizeof(fCompleted));
        if (hr != S_OK || !fCompleted)
            break; /* Queries are completed in order, so if this one is still running, then the next queries are too. */

        /* Reclaim the blocks which were used by this batch. */
        VBOXDXTRANSIENTHEAPBLOCKDESC *pBlock, *pBlockNext;
        RTListForEachSafe(&pBatch->listPendingBlocks, pBlock, pBlockNext, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientBlock)
        {
            RTListNodeRemove(&pBlock->nodeTransientBlock);
            dxTransientBlockRelease(pBlock);
        }

        dxTransientHeapBatchRetire(pBatch);
    }

    /* Submit the current batch query. */
    pBatch = pHeap->pCurrentBatch;
    if (pBatch && !RTListIsEmpty(&pBatch->listPendingBlocks))
    {
        vboxDXQueryEnd(pHeap->pDevice, &pBatch->queryBatchCompleted);

        RTListAppend(&pHeap->listTransientBatches, &pBatch->nodeTransientBatch);

        pHeap->pCurrentBatch = dxTransientHeapBatchGet(pHeap);
    }
}


/* Commands which are appended at the end of each command buffer. */
#pragma pack(1)
typedef struct VBOXDXCMDBUFSUFFIX
{
    struct
    {
        SVGA3dCmdHeader header;
        SVGA3dCmdDXMobFence64 cmd;
    } mobFence;
} VBOXDXCMDBUFSUFFIX;
#pragma pack()

#define VBOXDX_CMD_BUF_SUFFIX_ALLOCATION_COUNT 1
#define VBOXDX_CMD_BUF_SUFFIX_PATCH_COUNT 1


static void vboxdxCommandBufferFinalize(PVBOXDX_DEVICE pDevice)
{
    LogFlowFunc(("CONTEXTMONITORING: pDevice %p, with fence %RX64\n", pDevice, pDevice->ContextMonitoring.CurrentFenceValue));

    /* Append a MOB_FENCE command at the end of the command buffer. */
    VBOXDXCMDBUFSUFFIX *s = (VBOXDXCMDBUFSUFFIX *)((uint8_t *)pDevice->pCommandBuffer + pDevice->cbCommandBuffer);
    s->mobFence.header.id     = SVGA_3D_CMD_DX_MOB_FENCE_64;
    s->mobFence.header.size   = sizeof(SVGA3dCmdDXMobFence64);
    s->mobFence.cmd.value     = pDevice->ContextMonitoring.CurrentFenceValue;
    s->mobFence.cmd.mobId     = SVGA3D_INVALID_ID;
    s->mobFence.cmd.mobOffset = pDevice->ContextMonitoring.queryContextMonitoring.offQuery;

    vboxDXStorePatchLocation(pDevice, &s->mobFence.cmd.mobId,
                             pDevice->ContextMonitoring.queryContextMonitoring.pCOAllocation,
                             s->mobFence.cmd.mobOffset, true);

    Assert(pDevice->cbCommandReserved == 0);
    pDevice->cbCommandBuffer += sizeof(*s);
    pDevice->cbCommandReserved = 0;

    ++pDevice->ContextMonitoring.CurrentFenceValue;
}


static HRESULT vboxDXDeviceRender(PVBOXDX_DEVICE pDevice)
{
    LogFlowFunc(("pDevice %p, cbCommandBuffer %u, cAllocations %u, cPatchLocations %u\n",
        pDevice, pDevice->cbCommandBuffer, pDevice->cAllocations, pDevice->cPatchLocations));

    vboxdxCommandBufferFinalize(pDevice);

#ifdef LOG_ENABLED
    for (uint32_t i = 0; i < pDevice->cAllocations; ++i)
    {
        D3DDDI_ALLOCATIONLIST *p = &pDevice->pAllocationList[i];
        LogFlowFunc(("[%u] 0x%08x\n", i, p->hAllocation));
        RT_NOREF(p);
    }
#endif

    D3DDDICB_RENDER ddiRender;
    RT_ZERO(ddiRender);
    ddiRender.CommandLength     = pDevice->cbCommandBuffer;
    //ddiRender.CommandOffset     = 0;
    ddiRender.NumAllocations    = pDevice->cAllocations;
    ddiRender.NumPatchLocations = pDevice->cPatchLocations;
    //ddiRender.Flags             = 0;
    ddiRender.hContext          = pDevice->hContext;

    HRESULT hr = pDevice->pRTCallbacks->pfnRenderCb(pDevice->hRTDevice.handle, &ddiRender);
    AssertReturn(SUCCEEDED(hr), hr);

    pDevice->pCommandBuffer        = ddiRender.pNewCommandBuffer;
    pDevice->CommandBufferSize     = ddiRender.NewCommandBufferSize - sizeof(VBOXDXCMDBUFSUFFIX);
    pDevice->pAllocationList       = ddiRender.pNewAllocationList;
    pDevice->AllocationListSize    = ddiRender.NewAllocationListSize - VBOXDX_CMD_BUF_SUFFIX_ALLOCATION_COUNT;
    pDevice->pPatchLocationList    = ddiRender.pNewPatchLocationList;
    pDevice->PatchLocationListSize = ddiRender.NewPatchLocationListSize - VBOXDX_CMD_BUF_SUFFIX_PATCH_COUNT;

    Assert(pDevice->cbCommandReserved == 0);
    pDevice->cbCommandBuffer = 0;
    pDevice->cAllocations    = 0;
    pDevice->cPatchLocations = 0;

    ++pDevice->RenderCbSequence;

    return S_OK;
}


void *vboxDXCommandBufferReserve(PVBOXDX_DEVICE pDevice, SVGAFifo3dCmdId enmCmd, uint32_t cbCmd, uint32_t cPatchLocations)
{
    Assert(pDevice->cbCommandBuffer <= pDevice->CommandBufferSize);

    uint32_t const cbReserve = sizeof(SVGA3dCmdHeader) + cbCmd;
    uint32_t cbAvail = pDevice->CommandBufferSize - pDevice->cbCommandBuffer;
    if (   cbAvail < cbReserve
        || pDevice->PatchLocationListSize - pDevice->cPatchLocations < cPatchLocations
        || pDevice->AllocationListSize - pDevice->cAllocations < cPatchLocations)
    {
        HRESULT hr = vboxDXDeviceRender(pDevice);
        if (FAILED(hr))
            return NULL;
        cbAvail = pDevice->CommandBufferSize - pDevice->cbCommandBuffer;
        AssertReturn(cbAvail >= cbReserve, NULL);
    }

    pDevice->cbCommandReserved = cbReserve;

    SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)((uint8_t *)pDevice->pCommandBuffer + pDevice->cbCommandBuffer);
    pHeader->id = enmCmd;
    pHeader->size = cbCmd;
    return &pHeader[1];
}


void vboxDXCommandBufferCommit(PVBOXDX_DEVICE pDevice)
{
    Assert(pDevice->cbCommandBuffer <= pDevice->CommandBufferSize);
    Assert(pDevice->cbCommandReserved <= pDevice->CommandBufferSize - pDevice->cbCommandBuffer);
    pDevice->cbCommandBuffer += pDevice->cbCommandReserved;
    pDevice->cbCommandReserved = 0;
}


void vboxDXStorePatchLocation(PVBOXDX_DEVICE pDevice, void *pvPatch, PVBOXDXKMRESOURCE pKMResource,
                              uint32_t offAllocation, bool fWriteOperation, uint32_t DriverId)
{
    D3DKMT_HANDLE hAllocation = vboxDXGetAllocation(pKMResource);
    if (!hAllocation)
        return;

    /* Find the same hAllocation */
    int idxAllocation = -1;
    for (unsigned i = 0; i < pDevice->cAllocations; ++i)
    {
         D3DDDI_ALLOCATIONLIST *p = &pDevice->pAllocationList[i];
         if (p->hAllocation == hAllocation)
         {
             idxAllocation = i;
             break;
         }
    }

    /* If allocation is already in the list, then do not touch its WriteOperation flag.
     * Trying to do 'pAllocationEntry->WriteOperation |= fWriteOperation' caused
     * problems when opening Windows 10 start menu and when switching between Windows 8
     * desktop and tile screens.
     */
    if (idxAllocation < 0)
    {
        /* Add allocation to the list. */
        idxAllocation = pDevice->cAllocations++;

        D3DDDI_ALLOCATIONLIST *pAllocationEntry = &pDevice->pAllocationList[idxAllocation];
        pAllocationEntry->hAllocation = hAllocation;
        pAllocationEntry->Value = 0;
        pAllocationEntry->WriteOperation = fWriteOperation;

        /* Remember the fence value for resources used in this submission to track resource use. */
        pKMResource->LastReferencedFenceValue = pDevice->ContextMonitoring.CurrentFenceValue;
    }

    /* Add an optional patch location. */
    if (pvPatch)
    {
        D3DDDI_PATCHLOCATIONLIST *pPatchLocation = &pDevice->pPatchLocationList[pDevice->cPatchLocations];
        pPatchLocation->AllocationIndex = idxAllocation;
        pPatchLocation->Value = 0;
        pPatchLocation->DriverId = DriverId == 0
                                 ? pKMResource->AllocationDesc.enmAllocationType
                                 : DriverId;
        pPatchLocation->AllocationOffset = offAllocation;
        pPatchLocation->PatchOffset = (uintptr_t)pvPatch - (uintptr_t)pDevice->pCommandBuffer;
        pPatchLocation->SplitOffset = pDevice->cbCommandBuffer;
        ++pDevice->cPatchLocations;
    }

    /* Move the KM resource to the head of the resource list. */
    RTListNodeRemove(&pKMResource->nodeResource);
    RTListPrepend(&pDevice->listResources, &pKMResource->nodeResource);
}


DECLINLINE(bool) dxIsResourceInUse(PVBOXDX_DEVICE pDevice, PVBOXDXKMRESOURCE pKMResource)
{
    uint64_t const u64LastCompletedFenceValue = ASMAtomicReadU64(pDevice->ContextMonitoring.pLastCompletedFenceValue);
    return pKMResource->LastReferencedFenceValue > u64LastCompletedFenceValue;
}


HRESULT vboxDXDeviceFlushCommands(PVBOXDX_DEVICE pDevice)
{
    dxTransientHeapOnFlush(&pDevice->transientHeap);

    /* Check which upload batches have been processed by the host. */
    VBOXDXUPLOADBATCH *pBatch, *pNext;
    RTListForEachSafe(&pDevice->upload.listUploadBatches, pBatch, pNext, VBOXDXUPLOADBATCH, nodeUploadBatch)
    {
        BOOL fCompleted = FALSE;
        HRESULT hr = vboxdxQueryGetDataInternal(pDevice, &pBatch->queryBatchCompleted,
                                                &fCompleted, sizeof(fCompleted));
        if (hr != S_OK || !fCompleted)
            break; /* Queries are completed in order, so if this one is still running, then the next queries are too. */

        /* Reclaim the space which was used by this batch. */
        pDevice->upload.cbFree += pBatch->cbBatch;

        dxUploadBatchRetire(pDevice, pBatch);
    }

    /* Submit the current upload batch query on flush. */
    pBatch = pDevice->upload.pCurrentBatch;
    if (pBatch && pBatch->cbBatch)
    {
        vboxDXQueryEnd(pDevice, &pBatch->queryBatchCompleted);

        RTListAppend(&pDevice->upload.listUploadBatches, &pBatch->nodeUploadBatch);

        pDevice->upload.pCurrentBatch = dxUploadBatchGet(pDevice);
    }

    return vboxDXDeviceRender(pDevice);
}


static void vboxDXEmitSetConstantBuffers(PVBOXDX_DEVICE pDevice)
{
    for (unsigned idxShaderType = 0; idxShaderType < RT_ELEMENTS(pDevice->pipeline.aConstantBuffers); ++idxShaderType)
    {
        SVGA3dShaderType const enmShaderType = (SVGA3dShaderType)(idxShaderType + SVGA3D_SHADERTYPE_MIN);

        PVBOXDXCONSTANTBUFFERSSTATE pCBS = &pDevice->pipeline.aConstantBuffers[idxShaderType];
        for (unsigned i = pCBS->StartSlot; i < pCBS->StartSlot + pCBS->NumBuffers; ++i)
        {
            PVBOXDX_RESOURCE pResource = pCBS->apResource[i];
            if (pResource)
            {
                PVBOXDXKMRESOURCE const pKMResource = vboxDXGetKMResource(pResource);
                uint32 const offsetInBytes = pCBS->aFirstConstant[i] * (4 * sizeof(UINT));
                uint32 const sizeInBytes = pCBS->aNumConstants[i] * (4 * sizeof(UINT));
                LogFunc(("type %d, slot %d, off %d, size %d, cbAllocation %d",
                         enmShaderType, i, offsetInBytes, sizeInBytes, pKMResource->AllocationDesc.cbAllocation));

                vgpu10SetSingleConstantBuffer(pDevice, i, enmShaderType, pKMResource, offsetInBytes, sizeInBytes);
            }
            else
                vgpu10SetSingleConstantBuffer(pDevice, i, enmShaderType, 0, 0, 0);
        }

        /* Trim empty slots. */
        while (pCBS->NumBuffers)
        {
            if (pCBS->apResource[pCBS->StartSlot + pCBS->NumBuffers - 1])
                break;
            --pCBS->NumBuffers;
        }

        while (pCBS->NumBuffers)
        {
            if (pCBS->apResource[pCBS->StartSlot])
                break;
            --pCBS->NumBuffers;
            ++pCBS->StartSlot;
        }
    }
}


static void vboxDXEmitSetVertexBuffers(PVBOXDX_DEVICE pDevice)
{
    PVBOXDXVERTEXBUFFERSSTATE pVBS = &pDevice->pipeline.VertexBuffers;

    /* Fetch allocation handles. */
    PVBOXDXKMRESOURCE aKMResources[SVGA3D_MAX_VERTEX_ARRAYS];
    for (unsigned i = pVBS->StartSlot; i < pVBS->StartSlot + pVBS->NumBuffers; ++i)
    {
        PVBOXDX_RESOURCE pResource = pVBS->apResource[i];
        aKMResources[i] = vboxDXGetKMResource(pResource);
    }

    vgpu10SetVertexBuffers(pDevice, pVBS->StartSlot, pVBS->NumBuffers, aKMResources,
                           &pVBS->aStrides[pVBS->StartSlot], &pVBS->aOffsets[pVBS->StartSlot]);

    /* Trim empty slots. */
    while (pVBS->NumBuffers)
    {
        if (pVBS->apResource[pVBS->StartSlot + pVBS->NumBuffers - 1])
            break;
        --pVBS->NumBuffers;
    }

    while (pVBS->NumBuffers)
    {
        if (pVBS->apResource[pVBS->StartSlot])
            break;
        --pVBS->NumBuffers;
        ++pVBS->StartSlot;
    }
}


static void vboxDXEmitSetIndexBuffer(PVBOXDX_DEVICE pDevice)
{
    PVBOXDXINDEXBUFFERSTATE pIBS = &pDevice->pipeline.IndexBuffer;

    PVBOXDXKMRESOURCE const pKMResource = vboxDXGetKMResource(pIBS->pBuffer);
    SVGA3dSurfaceFormat const svgaFormat = vboxDXDxgiToSvgaFormat(pIBS->Format);
    vgpu10SetIndexBuffer(pDevice, pKMResource, svgaFormat, pIBS->Offset);
}


static void vboxDXSetupPipeline(PVBOXDX_DEVICE pDevice)
{
    vboxDXEmitSetConstantBuffers(pDevice);
    vboxDXEmitSetVertexBuffers(pDevice);
    vboxDXEmitSetIndexBuffer(pDevice);
}


SVGA3dSurfaceFormat vboxDXDxgiToSvgaFormat(DXGI_FORMAT enmDxgiFormat)
{
    switch (enmDxgiFormat)
    {
        case DXGI_FORMAT_UNKNOWN:                    return SVGA3D_BUFFER;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:      return SVGA3D_R32G32B32A32_TYPELESS;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:         return SVGA3D_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_UINT:          return SVGA3D_R32G32B32A32_UINT;
        case DXGI_FORMAT_R32G32B32A32_SINT:          return SVGA3D_R32G32B32A32_SINT;
        case DXGI_FORMAT_R32G32B32_TYPELESS:         return SVGA3D_R32G32B32_TYPELESS;
        case DXGI_FORMAT_R32G32B32_FLOAT:            return SVGA3D_R32G32B32_FLOAT;
        case DXGI_FORMAT_R32G32B32_UINT:             return SVGA3D_R32G32B32_UINT;
        case DXGI_FORMAT_R32G32B32_SINT:             return SVGA3D_R32G32B32_SINT;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:      return SVGA3D_R16G16B16A16_TYPELESS;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:         return SVGA3D_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_UNORM:         return SVGA3D_R16G16B16A16_UNORM;
        case DXGI_FORMAT_R16G16B16A16_UINT:          return SVGA3D_R16G16B16A16_UINT;
        case DXGI_FORMAT_R16G16B16A16_SNORM:         return SVGA3D_R16G16B16A16_SNORM;
        case DXGI_FORMAT_R16G16B16A16_SINT:          return SVGA3D_R16G16B16A16_SINT;
        case DXGI_FORMAT_R32G32_TYPELESS:            return SVGA3D_R32G32_TYPELESS;
        case DXGI_FORMAT_R32G32_FLOAT:               return SVGA3D_R32G32_FLOAT;
        case DXGI_FORMAT_R32G32_UINT:                return SVGA3D_R32G32_UINT;
        case DXGI_FORMAT_R32G32_SINT:                return SVGA3D_R32G32_SINT;
        case DXGI_FORMAT_R32G8X24_TYPELESS:          return SVGA3D_R32G8X24_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:       return SVGA3D_D32_FLOAT_S8X24_UINT;
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:   return SVGA3D_R32_FLOAT_X8X24;
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:    return SVGA3D_X32_G8X24_UINT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:       return SVGA3D_R10G10B10A2_TYPELESS;
        case DXGI_FORMAT_R10G10B10A2_UNORM:          return SVGA3D_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R10G10B10A2_UINT:           return SVGA3D_R10G10B10A2_UINT;
        case DXGI_FORMAT_R11G11B10_FLOAT:            return SVGA3D_R11G11B10_FLOAT;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:          return SVGA3D_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_R8G8B8A8_UNORM:             return SVGA3D_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:        return SVGA3D_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_R8G8B8A8_UINT:              return SVGA3D_R8G8B8A8_UINT;
        case DXGI_FORMAT_R8G8B8A8_SNORM:             return SVGA3D_R8G8B8A8_SNORM;
        case DXGI_FORMAT_R8G8B8A8_SINT:              return SVGA3D_R8G8B8A8_SINT;
        case DXGI_FORMAT_R16G16_TYPELESS:            return SVGA3D_R16G16_TYPELESS;
        case DXGI_FORMAT_R16G16_FLOAT:               return SVGA3D_R16G16_FLOAT;
        case DXGI_FORMAT_R16G16_UNORM:               return SVGA3D_R16G16_UNORM;
        case DXGI_FORMAT_R16G16_UINT:                return SVGA3D_R16G16_UINT;
        case DXGI_FORMAT_R16G16_SNORM:               return SVGA3D_R16G16_SNORM;
        case DXGI_FORMAT_R16G16_SINT:                return SVGA3D_R16G16_SINT;
        case DXGI_FORMAT_R32_TYPELESS:               return SVGA3D_R32_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT:                  return SVGA3D_D32_FLOAT;
        case DXGI_FORMAT_R32_FLOAT:                  return SVGA3D_R32_FLOAT;
        case DXGI_FORMAT_R32_UINT:                   return SVGA3D_R32_UINT;
        case DXGI_FORMAT_R32_SINT:                   return SVGA3D_R32_SINT;
        case DXGI_FORMAT_R24G8_TYPELESS:             return SVGA3D_R24G8_TYPELESS;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:          return SVGA3D_D24_UNORM_S8_UINT;
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:      return SVGA3D_R24_UNORM_X8;
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:       return SVGA3D_X24_G8_UINT;
        case DXGI_FORMAT_R8G8_TYPELESS:              return SVGA3D_R8G8_TYPELESS;
        case DXGI_FORMAT_R8G8_UNORM:                 return SVGA3D_R8G8_UNORM;
        case DXGI_FORMAT_R8G8_UINT:                  return SVGA3D_R8G8_UINT;
        case DXGI_FORMAT_R8G8_SNORM:                 return SVGA3D_R8G8_SNORM;
        case DXGI_FORMAT_R8G8_SINT:                  return SVGA3D_R8G8_SINT;
        case DXGI_FORMAT_R16_TYPELESS:               return SVGA3D_R16_TYPELESS;
        case DXGI_FORMAT_R16_FLOAT:                  return SVGA3D_R16_FLOAT;
        case DXGI_FORMAT_D16_UNORM:                  return SVGA3D_D16_UNORM;
        case DXGI_FORMAT_R16_UNORM:                  return SVGA3D_R16_UNORM;
        case DXGI_FORMAT_R16_UINT:                   return SVGA3D_R16_UINT;
        case DXGI_FORMAT_R16_SNORM:                  return SVGA3D_R16_SNORM;
        case DXGI_FORMAT_R16_SINT:                   return SVGA3D_R16_SINT;
        case DXGI_FORMAT_R8_TYPELESS:                return SVGA3D_R8_TYPELESS;
        case DXGI_FORMAT_R8_UNORM:                   return SVGA3D_R8_UNORM;
        case DXGI_FORMAT_R8_UINT:                    return SVGA3D_R8_UINT;
        case DXGI_FORMAT_R8_SNORM:                   return SVGA3D_R8_SNORM;
        case DXGI_FORMAT_R8_SINT:                    return SVGA3D_R8_SINT;
        case DXGI_FORMAT_A8_UNORM:                   return SVGA3D_A8_UNORM;
        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:         return SVGA3D_R9G9B9E5_SHAREDEXP;
        case DXGI_FORMAT_R8G8_B8G8_UNORM:            return SVGA3D_R8G8_B8G8_UNORM;
        case DXGI_FORMAT_G8R8_G8B8_UNORM:            return SVGA3D_G8R8_G8B8_UNORM;
        case DXGI_FORMAT_BC1_TYPELESS:               return SVGA3D_BC1_TYPELESS;
        case DXGI_FORMAT_BC1_UNORM:                  return SVGA3D_BC1_UNORM;
        case DXGI_FORMAT_BC1_UNORM_SRGB:             return SVGA3D_BC1_UNORM_SRGB;
        case DXGI_FORMAT_BC2_TYPELESS:               return SVGA3D_BC2_TYPELESS;
        case DXGI_FORMAT_BC2_UNORM:                  return SVGA3D_BC2_UNORM;
        case DXGI_FORMAT_BC2_UNORM_SRGB:             return SVGA3D_BC2_UNORM_SRGB;
        case DXGI_FORMAT_BC3_TYPELESS:               return SVGA3D_BC3_TYPELESS;
        case DXGI_FORMAT_BC3_UNORM:                  return SVGA3D_BC3_UNORM;
        case DXGI_FORMAT_BC3_UNORM_SRGB:             return SVGA3D_BC3_UNORM_SRGB;
        case DXGI_FORMAT_BC4_TYPELESS:               return SVGA3D_BC4_TYPELESS;
        case DXGI_FORMAT_BC4_UNORM:                  return SVGA3D_BC4_UNORM;
        case DXGI_FORMAT_BC4_SNORM:                  return SVGA3D_BC4_SNORM;
        case DXGI_FORMAT_BC5_TYPELESS:               return SVGA3D_BC5_TYPELESS;
        case DXGI_FORMAT_BC5_UNORM:                  return SVGA3D_BC5_UNORM;
        case DXGI_FORMAT_BC5_SNORM:                  return SVGA3D_BC5_SNORM;
        case DXGI_FORMAT_B5G6R5_UNORM:               return SVGA3D_B5G6R5_UNORM;
        case DXGI_FORMAT_B5G5R5A1_UNORM:             return SVGA3D_B5G5R5A1_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM:             return SVGA3D_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_UNORM:             return SVGA3D_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return SVGA3D_R10G10B10_XR_BIAS_A2_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:          return SVGA3D_B8G8R8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:        return SVGA3D_B8G8R8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:          return SVGA3D_B8G8R8X8_TYPELESS;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:        return SVGA3D_B8G8R8X8_UNORM_SRGB;
        case DXGI_FORMAT_BC6H_TYPELESS:              return SVGA3D_BC6H_TYPELESS;
        case DXGI_FORMAT_BC6H_UF16:                  return SVGA3D_BC6H_UF16;
        case DXGI_FORMAT_BC6H_SF16:                  return SVGA3D_BC6H_SF16;
        case DXGI_FORMAT_BC7_TYPELESS:               return SVGA3D_BC7_TYPELESS;
        case DXGI_FORMAT_BC7_UNORM:                  return SVGA3D_BC7_UNORM;
        case DXGI_FORMAT_BC7_UNORM_SRGB:             return SVGA3D_BC7_UNORM_SRGB;
        case DXGI_FORMAT_AYUV:                       return SVGA3D_AYUV;
        case DXGI_FORMAT_NV12:                       return SVGA3D_NV12;
        case DXGI_FORMAT_420_OPAQUE:                 return SVGA3D_NV12;
        case DXGI_FORMAT_YUY2:                       return SVGA3D_YUY2;
        case DXGI_FORMAT_P8:                         return SVGA3D_P8;
        case DXGI_FORMAT_B4G4R4A4_UNORM:             return SVGA3D_B4G4R4A4_UNORM;

        /* Does not seem to be a corresponding format for these: */
        case DXGI_FORMAT_R1_UNORM:
        case DXGI_FORMAT_Y410:
        case DXGI_FORMAT_Y416:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
        case DXGI_FORMAT_NV11:
        case DXGI_FORMAT_AI44:
        case DXGI_FORMAT_IA44:
        case DXGI_FORMAT_A8P8:
        case DXGI_FORMAT_P208:
        case DXGI_FORMAT_V208:
        case DXGI_FORMAT_V408:
        case DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE:
        case DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE:
#ifdef NTDDI_WIN11_GE
        case DXGI_FORMAT_A4B4G4R4_UNORM: /* Added in SDK w11/26100 or thereabouts. */
#endif
        case DXGI_FORMAT_FORCE_UINT: /* warning */
            break;
    }
    DEBUG_BREAKPOINT_TEST();
    return SVGA3D_BUFFER;
}


D3DDDIFORMAT vboxDXDxgiToDDIFormat(DXGI_FORMAT enmDxgiFormat)
{
    switch (enmDxgiFormat)
    {
        case DXGI_FORMAT_UNKNOWN:                    return D3DDDIFMT_UNKNOWN;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:         return D3DDDIFMT_A32B32G32R32F;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:         return D3DDDIFMT_A16B16G16R16F;
        case DXGI_FORMAT_R32G32_FLOAT:               return D3DDDIFMT_G32R32F;
        case DXGI_FORMAT_R10G10B10A2_UNORM:          return D3DDDIFMT_A2B10G10R10;
        case DXGI_FORMAT_R8G8B8A8_UNORM:             return D3DDDIFMT_A8B8G8R8;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:        return D3DDDIFMT_A8B8G8R8;
        case DXGI_FORMAT_R16G16_UNORM:               return D3DDDIFMT_G16R16;
        case DXGI_FORMAT_D32_FLOAT:                  return D3DDDIFMT_D32F_LOCKABLE;
        case DXGI_FORMAT_R32_FLOAT:                  return D3DDDIFMT_R32F;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:          return D3DDDIFMT_D24S8;
        case DXGI_FORMAT_R16_FLOAT:                  return D3DDDIFMT_R16F;
        case DXGI_FORMAT_D16_UNORM:                  return D3DDDIFMT_D16;
        case DXGI_FORMAT_R8G8_B8G8_UNORM:            return D3DDDIFMT_G8R8_G8B8;
        case DXGI_FORMAT_G8R8_G8B8_UNORM:            return D3DDDIFMT_R8G8_B8G8;
        case DXGI_FORMAT_BC1_UNORM:                  return D3DDDIFMT_DXT1;
        case DXGI_FORMAT_BC1_UNORM_SRGB:             return D3DDDIFMT_DXT1;
        case DXGI_FORMAT_BC2_UNORM:                  return D3DDDIFMT_DXT2;
        case DXGI_FORMAT_BC2_UNORM_SRGB:             return D3DDDIFMT_DXT2;
        case DXGI_FORMAT_BC3_UNORM:                  return D3DDDIFMT_DXT3;
        case DXGI_FORMAT_BC3_UNORM_SRGB:             return D3DDDIFMT_DXT3;
        case DXGI_FORMAT_BC4_UNORM:                  return D3DDDIFMT_DXT4;
        case DXGI_FORMAT_BC4_SNORM:                  return D3DDDIFMT_DXT4;
        case DXGI_FORMAT_BC5_UNORM:                  return D3DDDIFMT_DXT5;
        case DXGI_FORMAT_BC5_SNORM:                  return D3DDDIFMT_DXT5;
        case DXGI_FORMAT_B5G6R5_UNORM:               return D3DDDIFMT_R5G6B5;
        case DXGI_FORMAT_B5G5R5A1_UNORM:             return D3DDDIFMT_A1R5G5B5;
        case DXGI_FORMAT_B8G8R8A8_UNORM:             return D3DDDIFMT_A8R8G8B8;
        case DXGI_FORMAT_B8G8R8X8_UNORM:             return D3DDDIFMT_X8R8G8B8;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:        return D3DDDIFMT_A8R8G8B8;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:        return D3DDDIFMT_X8R8G8B8;
        case DXGI_FORMAT_YUY2:                       return D3DDDIFMT_YUY2;
        case DXGI_FORMAT_P8:                         return D3DDDIFMT_P8;
        default:
            break;
    }
    return D3DDDIFMT_UNKNOWN;
}


PVBOXDXKMRESOURCE vboxDXAllocateKMResource(PVBOXDX_DEVICE pDevice, HANDLE hResource,
                                           PFNVBOXDXINITALLOCATIONDESC pfnInitAllocationDesc,
                                           void const *pvInitData, bool fZero)
{
    PVBOXDXKMRESOURCE pKMResource = (PVBOXDXKMRESOURCE)RTMemAllocZ(sizeof(VBOXDXKMRESOURCE));
    AssertReturnStmt(pKMResource, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), NULL);

    pfnInitAllocationDesc(&pKMResource->AllocationDesc, pvInitData);

    D3DDDI_ALLOCATIONINFO2 ddiAllocationInfo;
    RT_ZERO(ddiAllocationInfo);
    ddiAllocationInfo.pPrivateDriverData    = &pKMResource->AllocationDesc;
    ddiAllocationInfo.PrivateDriverDataSize = sizeof(pKMResource->AllocationDesc);
    if (pKMResource->AllocationDesc.fPrimary)
    {
        ddiAllocationInfo.VidPnSourceId     = pKMResource->AllocationDesc.PrimaryDesc.VidPnSourceId;
        ddiAllocationInfo.Flags.Primary     = 1;
    }

    if (pKMResource->AllocationDesc.resourceInfo.MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED)
        pKMResource->flags.fShared = 1;

    D3DDDICB_ALLOCATE ddiAllocate;
    RT_ZERO(ddiAllocate);
    ddiAllocate.hResource        = hResource;
    ddiAllocate.NumAllocations   = 1;
    ddiAllocate.pAllocationInfo2 = &ddiAllocationInfo;

    HRESULT hr = pDevice->pRTCallbacks->pfnAllocateCb(pDevice->hRTDevice.handle, &ddiAllocate);
    LogFlowFunc(("pfnAllocateCb returned 0x%x, hKMResource 0x%X, hAllocation 0x%X, primary %RTbool\n", hr, ddiAllocate.hKMResource, ddiAllocationInfo.hAllocation, pKMResource->AllocationDesc.fPrimary));
    AssertReturnStmt(SUCCEEDED(hr),
                     vboxDXDeallocateKMResource(pDevice, pKMResource); vboxDXDeviceSetError(pDevice, hr),
                     false);

    pKMResource->hResource = hResource;
    pKMResource->hAllocation = ddiAllocationInfo.hAllocation;

    if (fZero)
    {
        D3DDDICB_LOCK ddiLock;
        RT_ZERO(ddiLock);
        ddiLock.hAllocation = ddiAllocationInfo.hAllocation;
        ddiLock.Flags.WriteOnly = 1;
        hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
        if (SUCCEEDED(hr))
        {
            memset(ddiLock.pData, 0, pKMResource->AllocationDesc.cbAllocation);

            D3DDDICB_UNLOCK ddiUnlock;
            ddiUnlock.NumAllocations = 1;
            ddiUnlock.phAllocations = &ddiLock.hAllocation;
            hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        }
        AssertReturnStmt(SUCCEEDED(hr),
                         vboxDXDeallocateKMResource(pDevice, pKMResource); vboxDXDeviceSetError(pDevice, hr),
                         false);
    }

    return pKMResource;
}


PVBOXDXKMRESOURCE vboxDXOpenKMResource(PVBOXDX_DEVICE pDevice, HANDLE hResource, D3DKMT_HANDLE hAllocation,
                                       VBOXDXALLOCATIONDESC const *pDesc)
{
    PVBOXDXKMRESOURCE pKMResource = (PVBOXDXKMRESOURCE)RTMemAllocZ(sizeof(VBOXDXKMRESOURCE));
    AssertReturnStmt(pKMResource, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), NULL);

    pKMResource->hResource = hResource;
    pKMResource->hAllocation = hAllocation;
    pKMResource->AllocationDesc = *pDesc;
    pKMResource->flags.fShared = 1;
    return pKMResource;
}


void vboxDXDeallocateKMResource(PVBOXDX_DEVICE pDevice, PVBOXDXKMRESOURCE pKMResource)
{
    if (pKMResource)
    {
        if (pKMResource->hAllocation)
        {
            D3DDDICB_DEALLOCATE ddiDeallocate;
            RT_ZERO(ddiDeallocate);
            if (pKMResource->flags.fShared)
            {
                ddiDeallocate.hResource        = pKMResource->hResource;
                //ddiDeallocate.NumAllocations = 0;
                //ddiDeallocate.HandleList     = NULL;
            }
            else
            {
                //ddiDeallocate.hResource    = NULL;
                ddiDeallocate.NumAllocations = 1;
                ddiDeallocate.HandleList     = &pKMResource->hAllocation;
            }

            HRESULT hr = pDevice->pRTCallbacks->pfnDeallocateCb(pDevice->hRTDevice.handle, &ddiDeallocate);
            LogFlowFunc(("pfnDeallocateCb returned 0x%x, hAllocation 0x%x", hr, pKMResource->hAllocation));
            AssertStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));
        }

        RTMemFree(pKMResource);
    }
}


static uint32_t vboxDXCalcResourceAllocationSize(VBOXDXALLOCATIONDESC const *pAllocationDesc)
{
    /* The allocation holds the entire resource:
     *   (miplevel[0], ..., miplevel[MipLevels - 1])[0],
     *   ...,
     * (  miplevel[0], ..., miplevel[MipLevels - 1])[ArraySize - 1]
     */
    surf_size_struct base_level_size;
    base_level_size.width  = pAllocationDesc->surfaceInfo.size.width;
    base_level_size.height = pAllocationDesc->surfaceInfo.size.height;
    base_level_size.depth  = pAllocationDesc->surfaceInfo.size.depth;

    return svga3dsurface_get_serialized_size_extended(pAllocationDesc->surfaceInfo.format,
                                                      base_level_size,
                                                      pAllocationDesc->surfaceInfo.numMipLevels,
                                                      pAllocationDesc->surfaceInfo.arraySize,
                                                      pAllocationDesc->surfaceInfo.multisampleCount);
}


static SVGA3dSurfaceAllFlags vboxDXCalcSurfaceFlags(const D3D11DDIARG_CREATERESOURCE *pCreateResource)
{
    SVGA3dSurfaceAllFlags f = 0;

    UINT const BindFlags = pCreateResource->BindFlags;
    Assert((BindFlags & (  D3D11_DDI_BIND_PIPELINE_MASK
                         & ~(  D3D10_DDI_BIND_VERTEX_BUFFER
                             | D3D10_DDI_BIND_INDEX_BUFFER
                             | D3D10_DDI_BIND_CONSTANT_BUFFER
                             | D3D10_DDI_BIND_SHADER_RESOURCE
                             | D3D10_DDI_BIND_STREAM_OUTPUT
                             | D3D10_DDI_BIND_RENDER_TARGET
                             | D3D10_DDI_BIND_DEPTH_STENCIL
                             | D3D11_DDI_BIND_UNORDERED_ACCESS
                             | D3D11_DDI_BIND_DECODER))) == 0);

    if (BindFlags & D3D10_DDI_BIND_VERTEX_BUFFER)
        f |= SVGA3D_SURFACE_BIND_VERTEX_BUFFER | SVGA3D_SURFACE_HINT_VERTEXBUFFER;
    if (BindFlags & D3D10_DDI_BIND_INDEX_BUFFER)
        f |= SVGA3D_SURFACE_BIND_INDEX_BUFFER | SVGA3D_SURFACE_HINT_INDEXBUFFER;
    if (BindFlags & D3D10_DDI_BIND_CONSTANT_BUFFER)
        f |= SVGA3D_SURFACE_BIND_CONSTANT_BUFFER;
    if (BindFlags & D3D10_DDI_BIND_SHADER_RESOURCE)
        f |= SVGA3D_SURFACE_BIND_SHADER_RESOURCE;
    if (BindFlags & D3D10_DDI_BIND_STREAM_OUTPUT)
        f |= SVGA3D_SURFACE_BIND_STREAM_OUTPUT;
    if (BindFlags & D3D10_DDI_BIND_RENDER_TARGET)
        f |= SVGA3D_SURFACE_BIND_RENDER_TARGET | SVGA3D_SURFACE_HINT_RENDERTARGET;
    if (BindFlags & D3D10_DDI_BIND_DEPTH_STENCIL)
        f |= SVGA3D_SURFACE_BIND_DEPTH_STENCIL | SVGA3D_SURFACE_HINT_DEPTHSTENCIL;
    if (BindFlags & D3D11_DDI_BIND_UNORDERED_ACCESS)
        f |= SVGA3D_SURFACE_BIND_UAVIEW;
    if (BindFlags & D3D11_DDI_BIND_DECODER)
        f |= SVGA3D_SURFACE_RESERVED1;

    /* D3D10_DDI_BIND_PRESENT textures can be used as render targets in a blitter on the host. */
    if (BindFlags & D3D10_DDI_BIND_PRESENT)
        f |= SVGA3D_SURFACE_SCREENTARGET | SVGA3D_SURFACE_BIND_RENDER_TARGET | SVGA3D_SURFACE_HINT_RENDERTARGET;

    D3D10_DDI_RESOURCE_USAGE const Usage = (D3D10_DDI_RESOURCE_USAGE)pCreateResource->Usage;
    if (Usage == D3D10_DDI_USAGE_DEFAULT)
        f |= SVGA3D_SURFACE_HINT_INDIRECT_UPDATE;
    else if (Usage == D3D10_DDI_USAGE_IMMUTABLE)
        f |= SVGA3D_SURFACE_HINT_STATIC;
    else if (Usage == D3D10_DDI_USAGE_DYNAMIC)
        f |= SVGA3D_SURFACE_HINT_DYNAMIC;
    else if (Usage == D3D10_DDI_USAGE_STAGING)
        f |= SVGA3D_SURFACE_STAGING_UPLOAD | SVGA3D_SURFACE_STAGING_DOWNLOAD;

    D3D10DDIRESOURCE_TYPE const ResourceDimension = pCreateResource->ResourceDimension;
    if (ResourceDimension == D3D10DDIRESOURCE_TEXTURE1D)
        f |= SVGA3D_SURFACE_1D | SVGA3D_SURFACE_HINT_TEXTURE;
    else if (ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D)
        f |= SVGA3D_SURFACE_HINT_TEXTURE;
    else if (ResourceDimension == D3D10DDIRESOURCE_TEXTURE3D)
        f |= SVGA3D_SURFACE_VOLUME | SVGA3D_SURFACE_HINT_TEXTURE;
    else if (ResourceDimension == D3D10DDIRESOURCE_TEXTURECUBE)
        f |= SVGA3D_SURFACE_CUBEMAP | SVGA3D_SURFACE_HINT_TEXTURE;

    UINT const MiscFlags = pCreateResource->MiscFlags;
    if (MiscFlags & D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS)
        f |= SVGA3D_SURFACE_DRAWINDIRECT_ARGS;
    if (MiscFlags & D3D11_DDI_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
        f |= SVGA3D_SURFACE_BIND_RAW_VIEWS; /*  */
    if (MiscFlags & D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED)
        f |= SVGA3D_SURFACE_BUFFER_STRUCTURED;
    if (MiscFlags & D3D11_DDI_RESOURCE_MISC_RESOURCE_CLAMP)
        f |= SVGA3D_SURFACE_RESOURCE_CLAMP;

    if (pCreateResource->SampleDesc.Count > 1)
        f |= SVGA3D_SURFACE_MULTISAMPLE;

    return f;
}


static D3D10_DDI_RESOURCE_USAGE vboxDXSurfaceFlagsToResourceUsage(SVGA3dSurfaceAllFlags surfaceFlags)
{
    if (surfaceFlags & SVGA3D_SURFACE_HINT_INDIRECT_UPDATE) return D3D10_DDI_USAGE_DEFAULT;
    if (surfaceFlags & SVGA3D_SURFACE_HINT_STATIC)          return D3D10_DDI_USAGE_IMMUTABLE;
    if (surfaceFlags & SVGA3D_SURFACE_HINT_DYNAMIC)         return D3D10_DDI_USAGE_DYNAMIC;
    if (surfaceFlags & (SVGA3D_SURFACE_STAGING_UPLOAD | SVGA3D_SURFACE_STAGING_DOWNLOAD))
                                                            return D3D10_DDI_USAGE_STAGING;
    AssertFailedReturn(D3D10_DDI_USAGE_STAGING);
}


static D3D10DDIRESOURCE_TYPE vboxDXSurfaceFlagsToResourceDimension(SVGA3dSurfaceAllFlags surfaceFlags)
{
    if (surfaceFlags & SVGA3D_SURFACE_1D)           return D3D10DDIRESOURCE_TEXTURE1D;
    if (surfaceFlags & SVGA3D_SURFACE_VOLUME)       return D3D10DDIRESOURCE_TEXTURE3D;
    if (surfaceFlags & SVGA3D_SURFACE_CUBEMAP)      return D3D10DDIRESOURCE_TEXTURECUBE;
    if (surfaceFlags & SVGA3D_SURFACE_HINT_TEXTURE) return D3D10DDIRESOURCE_TEXTURE2D;
    /** @todo D3D11DDIRESOURCE_BUFFEREX? */
    return D3D10DDIRESOURCE_BUFFER;
}


static void resourceAllocationDesc(VBOXDXALLOCATIONDESC *pDesc, void const *pvInitData)
{
    const D3D11DDIARG_CREATERESOURCE *pCreateResource = (const D3D11DDIARG_CREATERESOURCE *)pvInitData;

    /* Init surface information which will be used by the miniport to define the surface. */
    pDesc->surfaceInfo.surfaceFlags       = vboxDXCalcSurfaceFlags(pCreateResource);
    pDesc->surfaceInfo.format             = vboxDXDxgiToSvgaFormat(pCreateResource->Format);
    pDesc->surfaceInfo.numMipLevels       = pCreateResource->MipLevels;
    pDesc->surfaceInfo.multisampleCount   = pCreateResource->SampleDesc.Count;
    if (pDesc->surfaceInfo.multisampleCount > 1)
    {
        pDesc->surfaceInfo.multisamplePattern = SVGA3D_MS_PATTERN_STANDARD;
        pDesc->surfaceInfo.qualityLevel       = SVGA3D_MS_QUALITY_FULL;
    }
    else
    {
        pDesc->surfaceInfo.multisamplePattern = SVGA3D_MS_PATTERN_NONE;
        pDesc->surfaceInfo.qualityLevel       = SVGA3D_MS_QUALITY_NONE;
    }
    pDesc->surfaceInfo.autogenFilter      = SVGA3D_TEX_FILTER_NONE;
    pDesc->surfaceInfo.size.width         = pCreateResource->pMipInfoList[0].TexelWidth;
    pDesc->surfaceInfo.size.height        = pCreateResource->pMipInfoList[0].TexelHeight;
    pDesc->surfaceInfo.size.depth         = pCreateResource->pMipInfoList[0].TexelDepth;
    pDesc->surfaceInfo.arraySize          = pCreateResource->ArraySize;
    pDesc->surfaceInfo.bufferByteStride   = pCreateResource->ByteStride;
    if (pCreateResource->pPrimaryDesc)
    {
         pDesc->fPrimary                  = true;
         pDesc->PrimaryDesc               = *pCreateResource->pPrimaryDesc;
    }
    else
         pDesc->fPrimary                  = false;
    pDesc->enmDDIFormat                   = vboxDXDxgiToDDIFormat(pCreateResource->Format);
    pDesc->resourceInfo.BindFlags         = pCreateResource->BindFlags;
    pDesc->resourceInfo.MapFlags          = pCreateResource->MapFlags;
    pDesc->resourceInfo.MiscFlags         = pCreateResource->MiscFlags;
    pDesc->resourceInfo.Format            = pCreateResource->Format;
    pDesc->resourceInfo.DecoderBufferType = pCreateResource->DecoderBufferType;

    /* Finally set the allocation type and compute the size. */
    pDesc->enmAllocationType = VBOXDXALLOCATIONTYPE_SURFACE;
    pDesc->cbAllocation = vboxDXCalcResourceAllocationSize(pDesc);
}


bool vboxDXCreateResource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource,
                          const D3D11DDIARG_CREATERESOURCE *pCreateResource)
{
    bool const fZero = pCreateResource->pInitialDataUP == NULL
                    && (   pCreateResource->Usage == D3D10_DDI_USAGE_DYNAMIC
                        || pCreateResource->Usage == D3D10_DDI_USAGE_STAGING);

    pResource->pKMResource = vboxDXAllocateKMResource(pDevice, pResource->hRTResource.handle,
                                                      resourceAllocationDesc, pCreateResource, fZero);
    if (!pResource->pKMResource)
    {
        /* Might be not enough memory due to temporary staging buffers. */
        vboxDXFlush(pDevice, true);
        pResource->pKMResource = vboxDXAllocateKMResource(pDevice, pResource->hRTResource.handle,
                                                          resourceAllocationDesc, pCreateResource, fZero);
    }

    if (!pResource->pKMResource)
        return false;

    pResource->ResourceDimension = pCreateResource->ResourceDimension;
    pResource->Usage             = (D3D10_DDI_RESOURCE_USAGE)pCreateResource->Usage;
    for (UINT i = 0; i < pCreateResource->MipLevels; ++i)
        pResource->aMipInfoList[i] = pCreateResource->pMipInfoList[i];
    pResource->cSubresources = pCreateResource->MipLevels * pCreateResource->ArraySize;
    pResource->uMap = 0;
    pResource->pMappedTransientHeapBlock = NULL;

    RTListInit(&pResource->listSRV);
    RTListInit(&pResource->listRTV);
    RTListInit(&pResource->listDSV);
    RTListInit(&pResource->listUAV);
    RTListInit(&pResource->listVDOV);
    RTListInit(&pResource->listVPIV);
    RTListInit(&pResource->listVPOV);

    pResource->pKMResource->resource.pResource = pResource;

    RTListAppend(&pDevice->listResources, &pResource->pKMResource->nodeResource);

    if (pCreateResource->pInitialDataUP)
    {
        /* Upload the data to the resource. */
        for (UINT i = 0; i < pResource->cSubresources; ++i)
            vboxDXResourceUpdateSubresourceUP(pDevice, pResource, i, NULL,
                                              pCreateResource->pInitialDataUP[i].pSysMem,
                                              pCreateResource->pInitialDataUP[i].SysMemPitch,
                                              pCreateResource->pInitialDataUP[i].SysMemSlicePitch, 0);

    }

    return true;
}


bool vboxDXOpenResource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource,
                        const D3D10DDIARG_OPENRESOURCE *pOpenResource)
{
    AssertReturnStmt(pOpenResource->NumAllocations == 1,
                     vboxDXDeviceSetError(pDevice, E_INVALIDARG), false);
    AssertReturnStmt(pOpenResource->pOpenAllocationInfo2[0].PrivateDriverDataSize == sizeof(VBOXDXALLOCATIONDESC),
                     vboxDXDeviceSetError(pDevice, E_INVALIDARG), false);

    pResource->pKMResource = vboxDXOpenKMResource(pDevice, pResource->hRTResource.handle, pOpenResource->pOpenAllocationInfo2[0].hAllocation,
                                                  (VBOXDXALLOCATIONDESC *)pOpenResource->pOpenAllocationInfo2[0].pPrivateDriverData);
    if (!pResource->pKMResource)
        return false;

    VBOXDXALLOCATIONDESC const *pDesc = vboxDXGetAllocationDesc(pResource);

    /* Restore resource data. */
    pResource->ResourceDimension = vboxDXSurfaceFlagsToResourceDimension(pDesc->surfaceInfo.surfaceFlags);
    pResource->Usage             = vboxDXSurfaceFlagsToResourceUsage(pDesc->surfaceInfo.surfaceFlags);
    for (UINT i = 0; i < pDesc->surfaceInfo.numMipLevels; ++i)
        RT_ZERO(pResource->aMipInfoList[i]);
    pResource->cSubresources = pDesc->surfaceInfo.numMipLevels * pDesc->surfaceInfo.arraySize;
    pResource->uMap = 0;
    pResource->pMappedTransientHeapBlock = NULL;

    RTListInit(&pResource->listSRV);
    RTListInit(&pResource->listRTV);
    RTListInit(&pResource->listDSV);
    RTListInit(&pResource->listUAV);
    RTListInit(&pResource->listVDOV);
    RTListInit(&pResource->listVPIV);
    RTListInit(&pResource->listVPOV);

    pResource->pKMResource->resource.pResource = pResource;

    RTListAppend(&pDevice->listResources, &pResource->pKMResource->nodeResource);

    return true;
}


/* Destroy a resource created by the system (via DDI). Shared resources are freed immediately.
 * Other resources are moved to the deferred destruction queue (pDevice->listDestroyedResources).
 * The 'pResource' structure will be deleted by D3D runtime in any case.
 */
void vboxDXDestroyResource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource)
{
    /* "the driver must process its deferred-destruction queue during calls to its Flush(D3D10) function"
     * Shared resources, which have to use  'D3DDDICB_DEALLOCATE::hResource', must be deleted immediately,
     * because D3D runtime will make 'hRTResource' invalid after this function returns.
     * Other resources, which can be deallocated using 'D3DDDICB_DEALLOCATE::HandleList', must be
     * added to the deferred destruction queue.
     */

    Assert(RTListIsEmpty(&pResource->listSRV));
    Assert(RTListIsEmpty(&pResource->listRTV));
    Assert(RTListIsEmpty(&pResource->listDSV));
    Assert(RTListIsEmpty(&pResource->listUAV));
    Assert(RTListIsEmpty(&pResource->listVDOV));
    Assert(RTListIsEmpty(&pResource->listVPIV));
    Assert(RTListIsEmpty(&pResource->listVPOV));

    /* Remove from the list of active resources. */
    RTListNodeRemove(&pResource->pKMResource->nodeResource);

    /* Remove from the list of resources to be offered. */
    if (pResource->pKMResource->flags.fPendingOffered)
    {
        pResource->pKMResource->flags.fPendingOffered = 0;
        RTListNodeRemove(&pResource->pKMResource->resource.nodeOffered);
    }

    if (pResource->pKMResource->flags.fShared)
    {
        /* Delete immediately. */
        if (dxIsResourceInUse(pDevice, pResource->pKMResource))
            vboxDXDeviceFlushCommands(pDevice);

        vboxDXDeallocateKMResource(pDevice, pResource->pKMResource);
        pResource->pKMResource = 0;
    }
    else
    {
        /* Set the resource for deferred destruction. */
        pResource->pKMResource->resource.pResource = NULL;
        RTListAppend(&pDevice->listDestroyedResources, &pResource->pKMResource->nodeResource);
    }
}


static SVGA3dDX11LogicOp d3dToSvgaLogicOp(D3D11_1_DDI_LOGIC_OP LogicOp)
{
    switch (LogicOp)
    {
        case D3D11_1_DDI_LOGIC_OP_CLEAR:         return SVGA3D_DX11_LOGICOP_CLEAR;
        case D3D11_1_DDI_LOGIC_OP_SET:           return SVGA3D_DX11_LOGICOP_SET;
        case D3D11_1_DDI_LOGIC_OP_COPY:          return SVGA3D_DX11_LOGICOP_COPY;
        case D3D11_1_DDI_LOGIC_OP_COPY_INVERTED: return SVGA3D_DX11_LOGICOP_COPY_INVERTED;
        case D3D11_1_DDI_LOGIC_OP_NOOP:          return SVGA3D_DX11_LOGICOP_NOOP;
        case D3D11_1_DDI_LOGIC_OP_INVERT:        return SVGA3D_DX11_LOGICOP_INVERT;
        case D3D11_1_DDI_LOGIC_OP_AND:           return SVGA3D_DX11_LOGICOP_AND;
        case D3D11_1_DDI_LOGIC_OP_NAND:          return SVGA3D_DX11_LOGICOP_NAND;
        case D3D11_1_DDI_LOGIC_OP_OR:            return SVGA3D_DX11_LOGICOP_OR;
        case D3D11_1_DDI_LOGIC_OP_NOR:           return SVGA3D_DX11_LOGICOP_NOR;
        case D3D11_1_DDI_LOGIC_OP_XOR:           return SVGA3D_DX11_LOGICOP_XOR;
        case D3D11_1_DDI_LOGIC_OP_EQUIV:         return SVGA3D_DX11_LOGICOP_EQUIV;
        case D3D11_1_DDI_LOGIC_OP_AND_REVERSE:   return SVGA3D_DX11_LOGICOP_AND_REVERSE;
        case D3D11_1_DDI_LOGIC_OP_AND_INVERTED:  return SVGA3D_DX11_LOGICOP_AND_INVERTED;
        case D3D11_1_DDI_LOGIC_OP_OR_REVERSE:    return SVGA3D_DX11_LOGICOP_OR_REVERSE;
        case D3D11_1_DDI_LOGIC_OP_OR_INVERTED:   return SVGA3D_DX11_LOGICOP_OR_INVERTED;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_DX11_LOGICOP_COPY;
}


static SVGA3dBlendOp d3dToSvgaBlend(D3D10_DDI_BLEND Blend)
{
    switch (Blend)
    {
        case D3D10_DDI_BLEND_ZERO:            return SVGA3D_BLENDOP_ZERO;
        case D3D10_DDI_BLEND_ONE:             return SVGA3D_BLENDOP_ONE;
        case D3D10_DDI_BLEND_SRC_COLOR:       return SVGA3D_BLENDOP_SRCCOLOR;
        case D3D10_DDI_BLEND_INV_SRC_COLOR:   return SVGA3D_BLENDOP_INVSRCCOLOR;
        case D3D10_DDI_BLEND_SRC_ALPHA:       return SVGA3D_BLENDOP_SRCALPHA;
        case D3D10_DDI_BLEND_INV_SRC_ALPHA:   return SVGA3D_BLENDOP_INVSRCALPHA;
        case D3D10_DDI_BLEND_DEST_ALPHA:      return SVGA3D_BLENDOP_DESTALPHA;
        case D3D10_DDI_BLEND_INV_DEST_ALPHA:  return SVGA3D_BLENDOP_INVDESTALPHA;
        case D3D10_DDI_BLEND_DEST_COLOR:      return SVGA3D_BLENDOP_DESTCOLOR;
        case D3D10_DDI_BLEND_INV_DEST_COLOR:  return SVGA3D_BLENDOP_INVDESTCOLOR;
        case D3D10_DDI_BLEND_SRC_ALPHASAT:    return SVGA3D_BLENDOP_SRCALPHASAT;
        case D3D10_DDI_BLEND_BLEND_FACTOR:    return SVGA3D_BLENDOP_BLENDFACTOR;
        case D3D10_DDI_BLEND_INVBLEND_FACTOR: return SVGA3D_BLENDOP_INVBLENDFACTOR;
        case D3D10_DDI_BLEND_SRC1_COLOR:      return SVGA3D_BLENDOP_SRC1COLOR;
        case D3D10_DDI_BLEND_INV_SRC1_COLOR:  return SVGA3D_BLENDOP_INVSRC1COLOR;
        case D3D10_DDI_BLEND_SRC1_ALPHA:      return SVGA3D_BLENDOP_SRC1ALPHA;
        case D3D10_DDI_BLEND_INV_SRC1_ALPHA:  return SVGA3D_BLENDOP_INVSRC1ALPHA;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_BLENDOP_ZERO;
}


static SVGA3dBlendEquation d3dToSvgaBlendEq(D3D10_DDI_BLEND_OP BlendOp)
{
    switch (BlendOp)
    {
        case D3D10_DDI_BLEND_OP_ADD:             return SVGA3D_BLENDEQ_ADD;
        case D3D10_DDI_BLEND_OP_SUBTRACT:        return SVGA3D_BLENDEQ_SUBTRACT;
        case D3D10_DDI_BLEND_OP_REV_SUBTRACT:    return SVGA3D_BLENDEQ_REVSUBTRACT;
        case D3D10_DDI_BLEND_OP_MIN:             return SVGA3D_BLENDEQ_MINIMUM;
        case D3D10_DDI_BLEND_OP_MAX:             return SVGA3D_BLENDEQ_MAXIMUM;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_BLENDEQ_ADD;
}


void vboxDXCreateBlendState(PVBOXDX_DEVICE pDevice, PVBOXDX_BLENDSTATE pBlendState)
{
    int rc = RTHandleTableAlloc(pDevice->hHTBlendState, pBlendState, &pBlendState->uBlendId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    D3D11_1_DDI_BLEND_DESC const *pBlendDesc = &pBlendState->BlendDesc;
    SVGA3dDXBlendStatePerRT perRT[SVGA3D_MAX_RENDER_TARGETS];
    AssertCompile(SVGA3D_MAX_RENDER_TARGETS == D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT);

    for (unsigned i = 0; i < D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        perRT[i].blendEnable           = pBlendDesc->RenderTarget[i].BlendEnable;
        perRT[i].srcBlend              = (uint8_t)d3dToSvgaBlend(pBlendDesc->RenderTarget[i].SrcBlend);
        perRT[i].destBlend             = (uint8_t)d3dToSvgaBlend(pBlendDesc->RenderTarget[i].DestBlend);
        perRT[i].blendOp               = (uint8_t)d3dToSvgaBlendEq(pBlendDesc->RenderTarget[i].BlendOp);
        perRT[i].srcBlendAlpha         = (uint8_t)d3dToSvgaBlend(pBlendDesc->RenderTarget[i].SrcBlendAlpha);
        perRT[i].destBlendAlpha        = (uint8_t)d3dToSvgaBlend(pBlendDesc->RenderTarget[i].DestBlendAlpha);
        perRT[i].blendOpAlpha          = (uint8_t)d3dToSvgaBlendEq(pBlendDesc->RenderTarget[i].BlendOpAlpha);
        perRT[i].renderTargetWriteMask = pBlendDesc->RenderTarget[i].RenderTargetWriteMask;
        perRT[i].logicOpEnable         = pBlendDesc->RenderTarget[i].LogicOpEnable;
        perRT[i].logicOp               = (uint8_t)d3dToSvgaLogicOp(pBlendDesc->RenderTarget[i].LogicOp);
    }

    vgpu10DefineBlendState(pDevice,
                           pBlendState->uBlendId,
                           pBlendDesc->AlphaToCoverageEnable,
                           pBlendDesc->IndependentBlendEnable,
                           perRT);
}


void vboxDXDestroyBlendState(PVBOXDX_DEVICE pDevice, PVBOXDX_BLENDSTATE pBlendState)
{
    vgpu10DestroyBlendState(pDevice, pBlendState->uBlendId);
    RTHandleTableFree(pDevice->hHTBlendState, pBlendState->uBlendId);
}


static SVGA3dComparisonFunc d3dToSvgaComparisonFunc(D3D10_DDI_COMPARISON_FUNC DepthFunc)
{
    switch (DepthFunc)
    {
        case D3D10_DDI_COMPARISON_NEVER:         return SVGA3D_COMPARISON_NEVER;
        case D3D10_DDI_COMPARISON_LESS:          return SVGA3D_COMPARISON_LESS;
        case D3D10_DDI_COMPARISON_EQUAL:         return SVGA3D_COMPARISON_EQUAL;
        case D3D10_DDI_COMPARISON_LESS_EQUAL:    return SVGA3D_COMPARISON_LESS_EQUAL;
        case D3D10_DDI_COMPARISON_GREATER:       return SVGA3D_COMPARISON_GREATER;
        case D3D10_DDI_COMPARISON_NOT_EQUAL:     return SVGA3D_COMPARISON_NOT_EQUAL;
        case D3D10_DDI_COMPARISON_GREATER_EQUAL: return SVGA3D_COMPARISON_GREATER_EQUAL;
        case D3D10_DDI_COMPARISON_ALWAYS:        return SVGA3D_COMPARISON_ALWAYS;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_COMPARISON_LESS;
}


static uint8_t d3dToSvgaStencilOp(D3D10_DDI_STENCIL_OP StencilOp)
{
    switch (StencilOp)
    {
        case D3D10_DDI_STENCIL_OP_KEEP:     return SVGA3D_STENCILOP_KEEP;
        case D3D10_DDI_STENCIL_OP_ZERO:     return SVGA3D_STENCILOP_ZERO;
        case D3D10_DDI_STENCIL_OP_REPLACE:  return SVGA3D_STENCILOP_REPLACE;
        case D3D10_DDI_STENCIL_OP_INCR_SAT: return SVGA3D_STENCILOP_INCRSAT;
        case D3D10_DDI_STENCIL_OP_DECR_SAT: return SVGA3D_STENCILOP_DECRSAT;
        case D3D10_DDI_STENCIL_OP_INVERT:   return SVGA3D_STENCILOP_INVERT;
        case D3D10_DDI_STENCIL_OP_INCR:     return SVGA3D_STENCILOP_INCR;
        case D3D10_DDI_STENCIL_OP_DECR:     return SVGA3D_STENCILOP_DECR;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_STENCILOP_KEEP;
}


void vboxDXCreateDepthStencilState(PVBOXDX_DEVICE pDevice, PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState)
{
    int rc = RTHandleTableAlloc(pDevice->hHTDepthStencilState, pDepthStencilState, &pDepthStencilState->uDepthStencilId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    D3D10_DDI_DEPTH_STENCIL_DESC *p = &pDepthStencilState->DepthStencilDesc;
    uint8_t depthEnable                   = p->DepthEnable;
    SVGA3dDepthWriteMask depthWriteMask   = p->DepthWriteMask;
    SVGA3dComparisonFunc depthFunc        = d3dToSvgaComparisonFunc(p->DepthFunc);
    uint8 stencilEnable                   = p->StencilEnable;
    uint8 frontEnable                     = p->FrontEnable;
    uint8 backEnable                      = p->BackEnable;
    uint8 stencilReadMask                 = p->StencilReadMask;
    uint8 stencilWriteMask                = p->StencilWriteMask;

    uint8 frontStencilFailOp              = d3dToSvgaStencilOp(p->FrontFace.StencilFailOp);
    uint8 frontStencilDepthFailOp         = d3dToSvgaStencilOp(p->FrontFace.StencilDepthFailOp);
    uint8 frontStencilPassOp              = d3dToSvgaStencilOp(p->FrontFace.StencilPassOp);
    SVGA3dComparisonFunc frontStencilFunc = d3dToSvgaComparisonFunc(p->FrontFace.StencilFunc);

    uint8 backStencilFailOp               = d3dToSvgaStencilOp(p->BackFace.StencilFailOp);
    uint8 backStencilDepthFailOp          = d3dToSvgaStencilOp(p->BackFace.StencilDepthFailOp);
    uint8 backStencilPassOp               = d3dToSvgaStencilOp(p->BackFace.StencilPassOp);
    SVGA3dComparisonFunc backStencilFunc  = d3dToSvgaComparisonFunc(p->BackFace.StencilFunc);

    vgpu10DefineDepthStencilState(pDevice,
                                  pDepthStencilState->uDepthStencilId,
                                  depthEnable,
                                  depthWriteMask,
                                  depthFunc,
                                  stencilEnable,
                                  frontEnable,
                                  backEnable,
                                  stencilReadMask,
                                  stencilWriteMask,
                                  frontStencilFailOp,
                                  frontStencilDepthFailOp,
                                  frontStencilPassOp,
                                  frontStencilFunc,
                                  backStencilFailOp,
                                  backStencilDepthFailOp,
                                  backStencilPassOp,
                                  backStencilFunc);
}


void vboxDXDestroyDepthStencilState(PVBOXDX_DEVICE pDevice, PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState)
{
    vgpu10DestroyDepthStencilState(pDevice, pDepthStencilState->uDepthStencilId);
    RTHandleTableFree(pDevice->hHTDepthStencilState, pDepthStencilState->uDepthStencilId);
}


static uint8_t d3dToSvgaFillMode(D3D10_DDI_FILL_MODE FillMode)
{
    switch (FillMode)
    {
        case D3D10_DDI_FILL_WIREFRAME: return SVGA3D_FILLMODE_LINE;
        case D3D10_DDI_FILL_SOLID:     return SVGA3D_FILLMODE_FILL;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_FILLMODE_FILL;
}


static SVGA3dCullMode d3dToSvgaCullMode(D3D10_DDI_CULL_MODE CullMode)
{
    switch (CullMode)
    {
        case D3D10_DDI_CULL_NONE:  return SVGA3D_CULL_NONE;
        case D3D10_DDI_CULL_FRONT: return SVGA3D_CULL_FRONT;
        case D3D10_DDI_CULL_BACK:  return SVGA3D_CULL_BACK;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_CULL_NONE;
}


void vboxDXCreateRasterizerState(PVBOXDX_DEVICE pDevice, PVBOXDX_RASTERIZER_STATE pRasterizerState)
{
    int rc = RTHandleTableAlloc(pDevice->hHTRasterizerState, pRasterizerState, &pRasterizerState->uRasterizerId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    D3D11_1_DDI_RASTERIZER_DESC *p = &pRasterizerState->RasterizerDesc;
    uint8 fillMode              = d3dToSvgaFillMode(p->FillMode);
    SVGA3dCullMode cullMode     = d3dToSvgaCullMode(p->CullMode);
    uint8 frontCounterClockwise = p->FrontCounterClockwise;
    uint8 provokingVertexLast   = 0; /** @todo */
    int32 depthBias             = p->DepthBias;
    float depthBiasClamp        = p->DepthBiasClamp;
    float slopeScaledDepthBias  = p->SlopeScaledDepthBias;
    uint8 depthClipEnable       = p->DepthClipEnable;
    uint8 scissorEnable         = p->ScissorEnable;
    SVGA3dMultisampleRastEnable multisampleEnable = p->MultisampleEnable;
    uint8 antialiasedLineEnable = p->AntialiasedLineEnable;
    float lineWidth             = 1.0f; /** @todo */
    uint8 lineStippleEnable     = 0; /** @todo */
    uint8 lineStippleFactor     = 0; /** @todo */
    uint16 lineStipplePattern   = 0; /** @todo */
    uint32 forcedSampleCount    = p->ForcedSampleCount;

    if (pDevice->pAdapter->fVBoxCaps & VBSVGA3D_CAP_RASTERIZER_STATE_V2)
    {
        vgpu10DefineRasterizerState_v2(pDevice,
                                       pRasterizerState->uRasterizerId,
                                       fillMode,
                                       cullMode,
                                       frontCounterClockwise,
                                       provokingVertexLast,
                                       depthBias,
                                       depthBiasClamp,
                                       slopeScaledDepthBias,
                                       depthClipEnable,
                                       scissorEnable,
                                       multisampleEnable,
                                       antialiasedLineEnable,
                                       lineWidth,
                                       lineStippleEnable,
                                       lineStippleFactor,
                                       lineStipplePattern,
                                       forcedSampleCount);
    }
    else
    {
        vgpu10DefineRasterizerState(pDevice,
                                    pRasterizerState->uRasterizerId,
                                    fillMode,
                                    cullMode,
                                    frontCounterClockwise,
                                    provokingVertexLast,
                                    depthBias,
                                    depthBiasClamp,
                                    slopeScaledDepthBias,
                                    depthClipEnable,
                                    scissorEnable,
                                    multisampleEnable,
                                    antialiasedLineEnable,
                                    lineWidth,
                                    lineStippleEnable,
                                    lineStippleFactor,
                                    lineStipplePattern);
    }
}


void vboxDXDestroyRasterizerState(PVBOXDX_DEVICE pDevice, PVBOXDX_RASTERIZER_STATE pRasterizerState)
{
    vgpu10DestroyRasterizerState(pDevice, pRasterizerState->uRasterizerId);
    RTHandleTableFree(pDevice->hHTRasterizerState, pRasterizerState->uRasterizerId);
}


static SVGA3dFilter d3dToSvgaFilter(D3D10_DDI_FILTER Filter)
{
    SVGA3dFilter f = 0;

    if (D3D10_DDI_DECODE_MIP_FILTER(Filter) == D3D10_DDI_FILTER_TYPE_LINEAR)
        f |= SVGA3D_FILTER_MIP_LINEAR;
    if (D3D10_DDI_DECODE_MAG_FILTER(Filter) == D3D10_DDI_FILTER_TYPE_LINEAR)
        f |= SVGA3D_FILTER_MAG_LINEAR;
    if (D3D10_DDI_DECODE_MIN_FILTER(Filter) == D3D10_DDI_FILTER_TYPE_LINEAR)
        f |= SVGA3D_FILTER_MIN_LINEAR;
    if (D3D10_DDI_DECODE_IS_ANISOTROPIC_FILTER(Filter))
        f |= SVGA3D_FILTER_ANISOTROPIC;
    if (D3D10_DDI_DECODE_IS_COMPARISON_FILTER(Filter))
        f |= SVGA3D_FILTER_COMPARE;
    Assert(D3DWDDM1_3DDI_DECODE_FILTER_REDUCTION(Filter) <= D3DWDDM1_3DDI_FILTER_REDUCTION_TYPE_COMPARISON);
    return f;
}


static uint8 d3dToSvgaTextureAddressMode(D3D10_DDI_TEXTURE_ADDRESS_MODE AddressMode)
{
    switch (AddressMode)
    {
        case D3D10_DDI_TEXTURE_ADDRESS_WRAP:       return SVGA3D_TEX_ADDRESS_WRAP;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRROR:     return SVGA3D_TEX_ADDRESS_MIRROR;
        case D3D10_DDI_TEXTURE_ADDRESS_CLAMP:      return SVGA3D_TEX_ADDRESS_CLAMP;
        case D3D10_DDI_TEXTURE_ADDRESS_BORDER:     return SVGA3D_TEX_ADDRESS_BORDER;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRRORONCE: return SVGA3D_TEX_ADDRESS_MIRRORONCE;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_TEX_ADDRESS_WRAP;
}


void vboxDXCreateSamplerState(PVBOXDX_DEVICE pDevice, PVBOXDX_SAMPLER_STATE pSamplerState)
{
    int rc = RTHandleTableAlloc(pDevice->hHTSamplerState, pSamplerState, &pSamplerState->uSamplerId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    D3D10_DDI_SAMPLER_DESC *p = &pSamplerState->SamplerDesc;
    SVGA3dFilter filter                 = d3dToSvgaFilter(p->Filter);
    uint8 addressU                      = d3dToSvgaTextureAddressMode(p->AddressU);
    uint8 addressV                      = d3dToSvgaTextureAddressMode(p->AddressV);
    uint8 addressW                      = d3dToSvgaTextureAddressMode(p->AddressW);
    float mipLODBias                    = p->MipLODBias;
    uint8 maxAnisotropy                 = p->MaxAnisotropy;
    SVGA3dComparisonFunc comparisonFunc = d3dToSvgaComparisonFunc(p->ComparisonFunc);
    SVGA3dRGBAFloat borderColor;
    borderColor.value[0]                = p->BorderColor[0];
    borderColor.value[1]                = p->BorderColor[1];
    borderColor.value[2]                = p->BorderColor[2];
    borderColor.value[3]                = p->BorderColor[3];
    float minLOD                        = p->MinLOD;
    float maxLOD                        = p->MaxLOD;

    vgpu10DefineSamplerState(pDevice,
                             pSamplerState->uSamplerId,
                             filter,
                             addressU,
                             addressV,
                             addressW,
                             mipLODBias,
                             maxAnisotropy,
                             comparisonFunc,
                             borderColor,
                             minLOD,
                             maxLOD);
}


void vboxDXDestroySamplerState(PVBOXDX_DEVICE pDevice, PVBOXDX_SAMPLER_STATE pSamplerState)
{
    vgpu10DestroySamplerState(pDevice, pSamplerState->uSamplerId);
    RTHandleTableFree(pDevice->hHTSamplerState, pSamplerState->uSamplerId);
}


void vboxDXCreateElementLayout(PVBOXDX_DEVICE pDevice, PVBOXDXELEMENTLAYOUT pElementLayout)
{
    int rc = RTHandleTableAlloc(pDevice->hHTElementLayout, pElementLayout, &pElementLayout->uElementLayoutId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    uint32_t const cElements = pElementLayout->NumElements;

    SVGA3dInputElementDesc *paDesc;
    if (cElements)
    {
        paDesc = (SVGA3dInputElementDesc *)RTMemTmpAlloc(cElements * sizeof(SVGA3dInputElementDesc));
        AssertReturnVoidStmt(paDesc != NULL,
            RTHandleTableFree(pDevice->hHTElementLayout, pElementLayout->uElementLayoutId);
            vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
    }
    else
        paDesc = NULL;

    for (unsigned i = 0; i < cElements; ++i)
    {
        D3D10DDIARG_INPUT_ELEMENT_DESC const *pSrc = &pElementLayout->aVertexElements[i];
        SVGA3dInputElementDesc *pDst = &paDesc[i];
        pDst->inputSlot            = pSrc->InputSlot;
        pDst->alignedByteOffset    = pSrc->AlignedByteOffset;
        pDst->format               = vboxDXDxgiToSvgaFormat(pSrc->Format);
        pDst->inputSlotClass       = pSrc->InputSlotClass;
        pDst->instanceDataStepRate = pSrc->InstanceDataStepRate;
        pDst->inputRegister        = pSrc->InputRegister;
    }

    vgpu10DefineElementLayout(pDevice, pElementLayout->uElementLayoutId, cElements, paDesc);
    RTMemTmpFree(paDesc);
}


void vboxDXDestroyElementLayout(PVBOXDX_DEVICE pDevice, PVBOXDXELEMENTLAYOUT pElementLayout)
{
    vgpu10DestroyElementLayout(pDevice, pElementLayout->uElementLayoutId);
    RTHandleTableFree(pDevice->hHTElementLayout, pElementLayout->uElementLayoutId);
}


void vboxDXSetInputLayout(PVBOXDX_DEVICE pDevice, PVBOXDXELEMENTLAYOUT pInputLayout)
{
    uint32_t const uElementLayoutId = pInputLayout ? pInputLayout->uElementLayoutId : SVGA3D_INVALID_ID;
    vgpu10SetInputLayout(pDevice, uElementLayoutId);
}


void vboxDXSetBlendState(PVBOXDX_DEVICE pDevice, PVBOXDX_BLENDSTATE pBlendState,
                         const FLOAT BlendFactor[4], UINT SampleMask)
{
    uint32_t const uBlendId = pBlendState ? pBlendState->uBlendId : SVGA3D_INVALID_ID;
    vgpu10SetBlendState(pDevice, uBlendId, BlendFactor, SampleMask);
}


void vboxDXSetDepthStencilState(PVBOXDX_DEVICE pDevice, PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState, UINT StencilRef)
{
    uint32_t const uDepthStencilId = pDepthStencilState ? pDepthStencilState->uDepthStencilId : SVGA3D_INVALID_ID;
    vgpu10SetDepthStencilState(pDevice, uDepthStencilId, StencilRef);
}


void vboxDXSetRasterizerState(PVBOXDX_DEVICE pDevice, PVBOXDX_RASTERIZER_STATE pRasterizerState)
{
    uint32_t const uRasterizerId = pRasterizerState ? pRasterizerState->uRasterizerId : SVGA3D_INVALID_ID;
    vgpu10SetRasterizerState(pDevice, uRasterizerId);
}


void vboxDXSetSamplers(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType,
                       UINT StartSlot, UINT NumSamplers, const uint32_t *paSamplerIds)
{
    vgpu10SetSamplers(pDevice, StartSlot, enmShaderType, NumSamplers, paSamplerIds);
}


static SVGA3dPrimitiveType d3dToSvgaPrimitiveType(D3D10_DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology)
{
    switch (PrimitiveTopology)
    {
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_UNDEFINED:                  return SVGA3D_PRIMITIVE_INVALID;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_POINTLIST:                  return SVGA3D_PRIMITIVE_POINTLIST;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST:                   return SVGA3D_PRIMITIVE_LINELIST;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINESTRIP:                  return SVGA3D_PRIMITIVE_LINESTRIP;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST:               return SVGA3D_PRIMITIVE_TRIANGLELIST;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:              return SVGA3D_PRIMITIVE_TRIANGLESTRIP;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:               return SVGA3D_PRIMITIVE_LINELIST_ADJ;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:              return SVGA3D_PRIMITIVE_LINESTRIP_ADJ;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:           return SVGA3D_PRIMITIVE_TRIANGLELIST_ADJ;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:          return SVGA3D_PRIMITIVE_TRIANGLESTRIP_ADJ;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_1_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_2_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_3_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_4_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_5_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_6_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_7_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_8_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_9_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_10_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_11_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_12_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_13_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_14_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_15_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_16_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_17_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_18_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_19_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_20_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_21_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_22_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_23_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_24_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_25_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_26_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_27_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_28_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_29_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_30_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_31_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_32_CONTROL_POINT_PATCH;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_PRIMITIVE_INVALID;
}


void vboxDXIaSetTopology(PVBOXDX_DEVICE pDevice, D3D10_DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology)
{
    SVGA3dPrimitiveType topology = d3dToSvgaPrimitiveType(PrimitiveTopology);
    vgpu10SetTopology(pDevice, topology);
}


void vboxDXDrawIndexed(PVBOXDX_DEVICE pDevice, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawIndexed(pDevice, IndexCount, StartIndexLocation, BaseVertexLocation);
}


void vboxDXDraw(PVBOXDX_DEVICE pDevice, UINT VertexCount, UINT StartVertexLocation)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10Draw(pDevice, VertexCount, StartVertexLocation);
}


void vboxDXDrawIndexedInstanced(PVBOXDX_DEVICE pDevice, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawIndexedInstanced(pDevice, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}


void vboxDXDrawInstanced(PVBOXDX_DEVICE pDevice, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawInstanced(pDevice, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}


void vboxDXDrawAuto(PVBOXDX_DEVICE pDevice)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawAuto(pDevice);
}


void vboxDXDrawIndexedInstancedIndirect(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT AlignedByteOffsetForArgs)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawIndexedInstancedIndirect(pDevice, vboxDXGetKMResource(pResource), AlignedByteOffsetForArgs);
}


void vboxDXDrawInstancedIndirect(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT AlignedByteOffsetForArgs)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawInstancedIndirect(pDevice, vboxDXGetKMResource(pResource), AlignedByteOffsetForArgs);
}


void vboxDXSetViewports(PVBOXDX_DEVICE pDevice, UINT NumViewports, UINT ClearViewports, const D3D10_DDI_VIEWPORT *pViewports)
{
    RT_NOREF(ClearViewports);
    vgpu10SetViewports(pDevice, NumViewports, pViewports);
}


void vboxDXSetScissorRects(PVBOXDX_DEVICE pDevice, UINT NumRects, UINT ClearRects, const D3D10_DDI_RECT *pRects)
{
    RT_NOREF(ClearRects);
    vgpu10SetScissorRects(pDevice, NumRects, pRects);
}


static void vboxDXDestroyCOAllocation(PVBOXDX_DEVICE pDevice, PVBOXDXKMRESOURCE pCOAllocation)
{
    if (pCOAllocation)
    {
        Assert(pCOAllocation->AllocationDesc.enmAllocationType == VBOXDXALLOCATIONTYPE_CO);

        if (pCOAllocation->co.pu8COMapped)
        {
            D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pCOAllocation);

            D3DDDICB_UNLOCK ddiUnlock;
            ddiUnlock.NumAllocations = 1;
            ddiUnlock.phAllocations = &hAllocation;
            HRESULT hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
            if (RT_LIKELY(SUCCEEDED(hr)))
                /* nothing */ ;
            else
                vboxDXDeviceSetError(pDevice, hr);
            pCOAllocation->co.pu8COMapped = NULL;
        }

        /* Remove from the global list of resources. */
        RTListNodeRemove(&pCOAllocation->nodeResource);
        vboxDXDeallocateKMResource(pDevice, pCOAllocation);
    }
}


static void coAllocationDesc(VBOXDXALLOCATIONDESC *pDesc, void const *pvInitData)
{
    pDesc->enmAllocationType = VBOXDXALLOCATIONTYPE_CO; /* Context Object allocation. */
    pDesc->cbAllocation      = *(uint32_t *)pvInitData;
}


static bool vboxDXCreateCOAllocation(PVBOXDX_DEVICE pDevice, RTLISTANCHOR *pList, PVBOXDXKMRESOURCE *ppCOAllocation, uint32_t cbAllocation, bool fMap = false)
{
    PVBOXDXKMRESOURCE pCOAllocation = vboxDXAllocateKMResource(pDevice, 0, coAllocationDesc, &cbAllocation, true);
    if (!pCOAllocation)
        return false;

    if (fMap)
    {
        D3DDDICB_LOCK ddiLock;
        RT_ZERO(ddiLock);
        ddiLock.hAllocation = vboxDXGetAllocation(pCOAllocation);
        ddiLock.Flags.IgnoreSync = 1;
        ddiLock.Flags.DonotWait = 1;

        HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
        if (RT_LIKELY(SUCCEEDED(hr)))
            pCOAllocation->co.pu8COMapped = (uint8_t *)ddiLock.pData;
        else
            AssertFailedReturnStmt(vboxDXDeallocateKMResource(pDevice, pCOAllocation);
                                   vboxDXDeviceSetError(pDevice, hr), false);
    }

    /* Initially the allocation contains one big free block and zero sized free blocks. */
    pCOAllocation->co.aOffset[0] = 0;
    for (unsigned i = 1; i < RT_ELEMENTS(pCOAllocation->co.aOffset); ++i)
        pCOAllocation->co.aOffset[i] = cbAllocation;

    /* Add to the chain of COAs. */
    RTListAppend(pList, &pCOAllocation->co.nodeAllocationsChain);

    /* Add to the global list of resources. */
    RTListAppend(&pDevice->listResources, &pCOAllocation->nodeResource);

    *ppCOAllocation = pCOAllocation;
    return true;
}

#define IS_CO_BLOCK_FREE(_a, _i) (((_a)->co.u64Bitmap & (1ULL << (_i))) == 0)
#define IS_CO_BLOCK_USED(_a, _i) (((_a)->co.u64Bitmap & (1ULL << (_i))) != 0)
#define SET_CO_BLOCK_FREE(_a, _i) (((_a)->co.u64Bitmap &= ~(1ULL << (_i))))
#define SET_CO_BLOCK_USED(_a, _i) (((_a)->co.u64Bitmap |= (1ULL << (_i))))

static bool vboxDXCOABlockAlloc(PVBOXDXKMRESOURCE pCOAllocation, uint32_t cb, uint32_t *poff)
{
    Assert(pCOAllocation->AllocationDesc.enmAllocationType == VBOXDXALLOCATIONTYPE_CO);

    //DEBUG_BREAKPOINT_TEST();
    /* Search for a big enough free block. The last block is a special case. */
    unsigned i = 0;
    for (; i < RT_ELEMENTS(pCOAllocation->co.aOffset) - 1; ++i)
    {
        if (   IS_CO_BLOCK_FREE(pCOAllocation, i)
            && pCOAllocation->co.aOffset[i + 1] - pCOAllocation->co.aOffset[i] >= cb)
        {
            /* Found one. */
            SET_CO_BLOCK_USED(pCOAllocation, i);

            /* If the next block is free, then add the remaining space to it. */
            if (IS_CO_BLOCK_FREE(pCOAllocation, i + 1))
                pCOAllocation->co.aOffset[i + 1] = pCOAllocation->co.aOffset[i] + cb;

            *poff = pCOAllocation->co.aOffset[i];
            return true;
        }
    }

    /* Last block. */
    if (   IS_CO_BLOCK_FREE(pCOAllocation, i)
        && pCOAllocation->AllocationDesc.cbAllocation - pCOAllocation->co.aOffset[i] >= cb)
    {
        /* Found one. */
        SET_CO_BLOCK_USED(pCOAllocation, i);

        *poff = pCOAllocation->co.aOffset[i];
        return true;
    }

    return false;
}


static void vboxDXCOABlockFree(PVBOXDXKMRESOURCE pCOAllocation, uint32_t offBlock)
{
    Assert(pCOAllocation->AllocationDesc.enmAllocationType == VBOXDXALLOCATIONTYPE_CO);

    //DEBUG_BREAKPOINT_TEST();
    for (unsigned i = 0; i < RT_ELEMENTS(pCOAllocation->co.aOffset); ++i)
    {
        if (pCOAllocation->co.aOffset[i] == offBlock)
        {
            Assert(IS_CO_BLOCK_USED(pCOAllocation, i));
            SET_CO_BLOCK_FREE(pCOAllocation, i);
            return;
        }
    }

    AssertFailed();
}


static void shadersAllocationDesc(VBOXDXALLOCATIONDESC *pDesc, void const *pvInitData)
{
    pDesc->enmAllocationType = VBOXDXALLOCATIONTYPE_SHADERS;
    pDesc->cbAllocation      = *(uint32_t *)pvInitData;
}


static uint32_t vboxdxNumberOfShadersAllocations(PVBOXDX_DEVICE pDevice)
{
    uint32_t cAllocations = 0;
    PVBOXDXKMRESOURCE pIter;
    RTListForEach(&pDevice->listShadersAllocations, pIter, VBOXDXKMRESOURCE, shaders.nodeAllocationsChain)
        ++cAllocations;
    return cAllocations;
}


static PVBOXDXKMRESOURCE vboxdxShadersAllocationFromOffset(PVBOXDX_DEVICE pDevice, uint32_t off)
{
    uint32_t offStart = 0;
    PVBOXDXKMRESOURCE pIter;
    RTListForEach(&pDevice->listShadersAllocations, pIter, VBOXDXKMRESOURCE, shaders.nodeAllocationsChain)
    {
        offStart += VBOXDX_SHADER_ALLOCATION_SIZE;
        if (off < offStart)
            return pIter;
    }
    return NULL;
}


static uint32_t vboxdxFindPlaceForShader(PVBOXDX_DEVICE pDevice, PVBOXDXSHADER pShader)
{
    uint32_t const cbShaderTotal = pShader->cbShader + pShader->cbSignatures;

    uint32_t offShader = SVGA3D_INVALID_ID; /* Offset in shaders allocations where to place the new shader. */

    /* First try to place the new shader right after the last shader in the same allocation. */
    uint32_t offFree = 0;
    PVBOXDXSHADER pLastShader = RTListGetLast(&pDevice->listShaders, VBOXDXSHADER, node);
    if (pLastShader)
    {
        offFree = pLastShader->offShader + pLastShader->cbShader + pLastShader->cbSignatures;
        uint32_t const cbRemaining = VBOXDX_SHADER_ALLOCATION_SIZE - offFree % VBOXDX_SHADER_ALLOCATION_SIZE;

        if (cbRemaining >= cbShaderTotal)
            offShader = offFree; /* Enough space. */
    }

    if (offShader == SVGA3D_INVALID_ID)
    {
        /* There is no space in the allocation where the currently last shader resides.
         * Check if there is an allocation after it. */
        offFree = RT_ALIGN_32(offFree, VBOXDX_SHADER_ALLOCATION_SIZE); /* Offset of the start of subsequent allocation. */

        uint32_t cShadersAllocations = vboxdxNumberOfShadersAllocations(pDevice);
        if (offFree / VBOXDX_SHADER_ALLOCATION_SIZE >= cShadersAllocations)
        {
            /* Allocate a new shaders allocation and place the new shader at the beginning of it. */
            uint32_t const cbAllocation = VBOXDX_SHADER_ALLOCATION_SIZE;
            PVBOXDXKMRESOURCE pShadersAllocation = vboxDXAllocateKMResource(pDevice, 0, shadersAllocationDesc,
                                                                            &cbAllocation, false);
            AssertReturnStmt(pShadersAllocation, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), SVGA3D_INVALID_ID);

            RTListAppend(&pDevice->listShadersAllocations, &pShadersAllocation->shaders.nodeAllocationsChain);

            /* Add to the global list of resources. */
            RTListAppend(&pDevice->listResources, &pShadersAllocation->nodeResource);
        }

        /* Place the new shader at the beginning of subsequent allocation. */
        offShader = offFree;
    }

    return offShader;
}


static SVGA3dDXSignatureSemanticName d3dToSvgaSemanticName(D3D10_SB_NAME SystemValue)
{
    /** @todo */
    return (SVGA3dDXSignatureSemanticName)SystemValue;
}


static SVGA3dDXSignatureRegisterComponentType d3dToSvgaComponentType(D3D10_SB_REGISTER_COMPONENT_TYPE RegisterComponentType)
{
    /** @todo */
    return (SVGA3dDXSignatureRegisterComponentType)RegisterComponentType;
}


static SVGA3dDXSignatureMinPrecision d3dToSvgaMinPrecision(D3D11_SB_OPERAND_MIN_PRECISION MinPrecision)
{
    /** @todo */
    return (SVGA3dDXSignatureMinPrecision)MinPrecision;
}


void vboxDXCreateShader(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, PVBOXDXSHADER pShader, const UINT* pShaderCode,
                        const D3D11_1DDIARG_SIGNATURE_ENTRY2* pInputSignature, UINT NumInputSignatureEntries,
                        const D3D11_1DDIARG_SIGNATURE_ENTRY2* pOutputSignature, UINT NumOutputSignatureEntries,
                        const D3D11_1DDIARG_SIGNATURE_ENTRY2* pPatchConstantSignature, UINT NumPatchConstantSignatureEntries)
{
    AssertReturnVoidStmt(   NumInputSignatureEntries <= 4096 /* An arbitrary large value. */
                         && NumOutputSignatureEntries <= 4096
                         && NumPatchConstantSignatureEntries <= 4096,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    /* CreateGeometryShaderWithStreamOutput sometimes passes pShaderCode == NULL. */
    pShader->enmShaderType = enmShaderType;
    pShader->cbShader = pShaderCode ? pShaderCode[1] * sizeof(UINT) : 0;
    pShader->cbSignatures = sizeof(SVGA3dDXSignatureHeader)
       + NumInputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
       + NumOutputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
       + NumPatchConstantSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry);
    AssertReturnVoidStmt(   pShader->cbShader < VBOXDX_SHADER_ALLOCATION_SIZE
                         && pShader->cbSignatures < VBOXDX_SHADER_ALLOCATION_SIZE,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    uint32_t const cbShaderTotal = pShader->cbShader + pShader->cbSignatures;
    AssertReturnVoidStmt(cbShaderTotal <= VBOXDX_SHADER_ALLOCATION_SIZE,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    if (pShader->enmShaderType == SVGA3D_SHADERTYPE_GS)
    {
        RT_ZERO(pShader->gs);
        pShader->gs.uStreamOutputId = SVGA3D_INVALID_ID;
        pShader->gs.offStreamOutputDecls = SVGA3D_INVALID_ID;
    }

    if (!pShaderCode)
    {
        RT_ZERO(pShader->node);
        pShader->uShaderId = SVGA3D_INVALID_ID;
        pShader->offShader = SVGA3D_INVALID_ID;
        pShader->pu32Bytecode = NULL;
        pShader->pSignatures = NULL;
        pShader->pShadersAllocation = NULL;
        return;
    }

    int rc = RTHandleTableAlloc(pDevice->hHTShader, pShader, &pShader->uShaderId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    /*
     * Find a place for the shader in a shaders allocation.
     */
    pShader->offShader = vboxdxFindPlaceForShader(pDevice, pShader);
    AssertReturnVoidStmt(pShader->offShader != SVGA3D_INVALID_ID,
                         RTHandleTableFree(pDevice->hHTShader, pShader->uShaderId));

    pShader->pShadersAllocation = vboxdxShadersAllocationFromOffset(pDevice, pShader->offShader);

    pShader->pu32Bytecode = (uint32_t *)&pShader[1];
    pShader->pSignatures = (SVGA3dDXSignatureHeader *)((uint8_t *)pShader->pu32Bytecode + pShader->cbShader);

    memcpy(pShader->pu32Bytecode, pShaderCode, pShader->cbShader);

    pShader->pSignatures->headerVersion = SVGADX_SIGNATURE_HEADER_VERSION_0;
    pShader->pSignatures->numInputSignatures = NumInputSignatureEntries;
    pShader->pSignatures->numOutputSignatures = NumOutputSignatureEntries;
    pShader->pSignatures->numPatchConstantSignatures = NumPatchConstantSignatureEntries;

    SVGA3dDXShaderSignatureEntry *pSignatureEntry = (SVGA3dDXShaderSignatureEntry *)&pShader->pSignatures[1];
    for (unsigned i = 0; i < NumInputSignatureEntries; ++i, ++pSignatureEntry)
    {
        pSignatureEntry->registerIndex = pInputSignature[i].Register;
        pSignatureEntry->semanticName  = d3dToSvgaSemanticName(pInputSignature[i].SystemValue);
        pSignatureEntry->mask          = pInputSignature[i].Mask;
        pSignatureEntry->componentType = d3dToSvgaComponentType(pInputSignature[i].RegisterComponentType);
        pSignatureEntry->minPrecision  = d3dToSvgaMinPrecision(pInputSignature[i].MinPrecision);
    }
    for (unsigned i = 0; i < NumOutputSignatureEntries; ++i, ++pSignatureEntry)
    {
        pSignatureEntry->registerIndex = pOutputSignature[i].Register;
        pSignatureEntry->semanticName  = d3dToSvgaSemanticName(pOutputSignature[i].SystemValue);
        pSignatureEntry->mask          = pOutputSignature[i].Mask;
        pSignatureEntry->componentType = d3dToSvgaComponentType(pOutputSignature[i].RegisterComponentType);
        pSignatureEntry->minPrecision  = d3dToSvgaMinPrecision(pOutputSignature[i].MinPrecision);
    }
    for (unsigned i = 0; i < NumPatchConstantSignatureEntries; ++i, ++pSignatureEntry)
    {
        pSignatureEntry->registerIndex = pPatchConstantSignature[i].Register;
        pSignatureEntry->semanticName  = d3dToSvgaSemanticName(pPatchConstantSignature[i].SystemValue);
        pSignatureEntry->mask          = pPatchConstantSignature[i].Mask;
        pSignatureEntry->componentType = d3dToSvgaComponentType(pPatchConstantSignature[i].RegisterComponentType);
        pSignatureEntry->minPrecision  = d3dToSvgaMinPrecision(pPatchConstantSignature[i].MinPrecision);
    }

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = vboxDXGetAllocation(pShader->pShadersAllocation);
    ddiLock.Flags.WriteOnly = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        uint8_t *pu8 = (uint8_t *)ddiLock.pData + pShader->offShader % VBOXDX_SHADER_ALLOCATION_SIZE;

        memcpy(pu8, pShader->pu32Bytecode, pShader->cbShader);
        pu8 += pShader->cbShader;

        memcpy(pu8, pShader->pSignatures, pShader->cbSignatures);

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &ddiLock.hAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
    }
    AssertReturnVoidStmt(SUCCEEDED(hr),
                         RTHandleTableFree(pDevice->hHTShader, pShader->uShaderId);
                         vboxDXDeviceSetError(pDevice, hr));

    RTListAppend(&pDevice->listShaders, &pShader->node);

    vgpu10DefineShader(pDevice, pShader->uShaderId, pShader->enmShaderType, cbShaderTotal);
    vgpu10BindShader(pDevice, pShader->uShaderId, pShader->pShadersAllocation, pShader->offShader % VBOXDX_SHADER_ALLOCATION_SIZE);
}


static void vboxDXHandleFree(RTHANDLETABLE hHT, uint32_t *pu32Id)
{
    RTHandleTableFree(hHT, *pu32Id);
    *pu32Id = SVGA3D_INVALID_ID;
}


void vboxDXCreateStreamOutput(PVBOXDX_DEVICE pDevice, PVBOXDXSHADER pShader,
                              const D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *pOutputStreamDecl, UINT NumEntries,
                              const UINT  *BufferStridesInBytes, UINT NumStrides,
                              UINT RasterizedStream)
{
    AssertReturnVoidStmt(NumEntries <= SVGA3D_MAX_STREAMOUT_DECLS, vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    pShader->gs.NumEntries       = NumEntries;
    pShader->gs.NumStrides       = RT_MIN(NumStrides, SVGA3D_DX_MAX_SOTARGETS);
    memcpy(pShader->gs.BufferStridesInBytes, BufferStridesInBytes, pShader->gs.NumStrides * sizeof(UINT));
    pShader->gs.RasterizedStream = RasterizedStream;

    int rc = RTHandleTableAlloc(pDevice->hHTStreamOutput, pShader, &pShader->gs.uStreamOutputId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    /* Allocate mob space for declarations. */
    pShader->gs.cbOutputStreamDecls = pShader->gs.NumEntries * sizeof(*pOutputStreamDecl);
    pShader->gs.pCOAllocation = NULL;
    PVBOXDXKMRESOURCE pIter;
    RTListForEach(&pDevice->listCOAStreamOutput, pIter, VBOXDXKMRESOURCE, co.nodeAllocationsChain)
    {
        if (vboxDXCOABlockAlloc(pIter, pShader->gs.cbOutputStreamDecls, &pShader->gs.offStreamOutputDecls))
        {
            pShader->gs.pCOAllocation = pIter;
            break;
        }
    }

    if (!pShader->gs.pCOAllocation)
    {
        /* Create a new allocation.  */
        if (!vboxDXCreateCOAllocation(pDevice, &pDevice->listCOAStreamOutput, &pShader->gs.pCOAllocation, 8 * pShader->gs.cbOutputStreamDecls))
            AssertFailedReturnVoidStmt(vboxDXHandleFree(pDevice->hHTStreamOutput, &pShader->gs.uStreamOutputId);
                                       vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

        if (!vboxDXCOABlockAlloc(pShader->gs.pCOAllocation, pShader->gs.cbOutputStreamDecls, &pShader->gs.offStreamOutputDecls))
            AssertFailedReturnVoidStmt(vboxDXHandleFree(pDevice->hHTStreamOutput, &pShader->gs.uStreamOutputId);
                                       vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
    }

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = vboxDXGetAllocation(pShader->gs.pCOAllocation);
    ddiLock.Flags.WriteOnly = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        uint8_t *pu8 = (uint8_t *)ddiLock.pData + pShader->gs.offStreamOutputDecls;

        D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY const *src = pOutputStreamDecl;
        SVGA3dStreamOutputDeclarationEntry *dst = (SVGA3dStreamOutputDeclarationEntry *)pu8;
        for (unsigned i = 0; i < pShader->gs.NumEntries; ++i)
        {
            dst->outputSlot    = src->OutputSlot;
            dst->registerIndex = src->RegisterIndex;
            dst->registerMask  = src->RegisterMask;
            dst->pad0          = 0;
            dst->pad1          = 0;
            dst->stream        = src->Stream;
            ++dst;
            ++src;
        }

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &ddiLock.hAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
    }
    AssertReturnVoidStmt(SUCCEEDED(hr),
                         vboxDXHandleFree(pDevice->hHTStreamOutput, &pShader->gs.uStreamOutputId);
                         vboxDXDeviceSetError(pDevice, hr));

    /* Inform host. */
    vgpu10DefineStreamOutputWithMob(pDevice, pShader->gs.uStreamOutputId, pShader->gs.NumEntries, pShader->gs.NumStrides,
                                    pShader->gs.BufferStridesInBytes, pShader->gs.RasterizedStream);
    vgpu10BindStreamOutput(pDevice, pShader->gs.uStreamOutputId, pShader->gs.pCOAllocation,
                           pShader->gs.offStreamOutputDecls, pShader->gs.cbOutputStreamDecls);
}


void vboxDXDestroyShader(PVBOXDX_DEVICE pDevice, PVBOXDXSHADER pShader)
{
    if (pShader->enmShaderType == SVGA3D_SHADERTYPE_GS)
    {
        if (pShader->gs.offStreamOutputDecls != SVGA3D_INVALID_ID)
        {
            vboxDXCOABlockFree(pShader->gs.pCOAllocation, pShader->gs.offStreamOutputDecls);
            pShader->gs.offStreamOutputDecls = SVGA3D_INVALID_ID;
            pShader->gs.pCOAllocation = NULL;
        }

        if (pShader->gs.uStreamOutputId != SVGA3D_INVALID_ID)
            vboxDXHandleFree(pDevice->hHTStreamOutput, &pShader->gs.uStreamOutputId);
    }

    if (pShader->uShaderId != SVGA3D_INVALID_ID)
    {
        /* Send VGPU commands. */
        vgpu10BindShader(pDevice, pShader->uShaderId, 0, 0);
        vgpu10DestroyShader(pDevice, pShader->uShaderId);
        vboxDXDeviceFlushCommands(pDevice);

        RTListNodeRemove(&pShader->node);
        RTHandleTableFree(pDevice->hHTShader, pShader->uShaderId);
    }
}

typedef struct VMSVGAQUERYINFO
{
    D3D10DDI_QUERY  queryTypeDDI;
    uint32_t        cbDataDDI;
    SVGA3dQueryType queryTypeSvga;
    uint32_t        cbDataSvga;
} VMSVGAQUERYINFO;

static VMSVGAQUERYINFO const *getQueryInfo(D3D10DDI_QUERY Query)
{
    static VMSVGAQUERYINFO const aQueryInfo[D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3 + 1] =
    {
        { D3D10DDI_QUERY_EVENT,                           sizeof(BOOL),
            SVGA3D_QUERYTYPE_INVALID,                        sizeof(UINT64) },
        { D3D10DDI_QUERY_OCCLUSION,                       sizeof(UINT64),
            SVGA3D_QUERYTYPE_OCCLUSION64,                    sizeof(SVGADXOcclusion64QueryResult) },
        { D3D10DDI_QUERY_TIMESTAMP,                       sizeof(UINT64),
            SVGA3D_QUERYTYPE_TIMESTAMP,                      sizeof(SVGADXTimestampQueryResult) },
        { D3D10DDI_QUERY_TIMESTAMPDISJOINT,               sizeof(D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT),
            SVGA3D_QUERYTYPE_TIMESTAMPDISJOINT,              sizeof(SVGADXTimestampDisjointQueryResult) },
        { D3D10DDI_QUERY_PIPELINESTATS,                   sizeof(D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS),
            SVGA3D_QUERYTYPE_PIPELINESTATS,                  sizeof(SVGADXPipelineStatisticsQueryResult) },
        { D3D10DDI_QUERY_OCCLUSIONPREDICATE,              sizeof(BOOL),
            SVGA3D_QUERYTYPE_OCCLUSIONPREDICATE,             sizeof(SVGADXOcclusionPredicateQueryResult) },
        { D3D10DDI_QUERY_STREAMOUTPUTSTATS,               sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_STREAMOUTPUTSTATS,              sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE,         sizeof(BOOL),
            SVGA3D_QUERYTYPE_STREAMOVERFLOWPREDICATE,        sizeof(SVGADXStreamOutPredicateQueryResult) },
        { D3D11DDI_QUERY_PIPELINESTATS,                   sizeof(D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS),
            SVGA3D_QUERYTYPE_PIPELINESTATS,                  sizeof(SVGADXPipelineStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0,       sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_SOSTATS_STREAM0,                sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM1,       sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_SOSTATS_STREAM1,                sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM2,       sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_SOSTATS_STREAM2,                sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM3,       sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_SOSTATS_STREAM3,                sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0, sizeof(BOOL),
            SVGA3D_QUERYTYPE_SOP_STREAM0,                    sizeof(SVGADXStreamOutPredicateQueryResult) },
        { D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM1, sizeof(BOOL),
            SVGA3D_QUERYTYPE_SOP_STREAM1,                    sizeof(SVGADXStreamOutPredicateQueryResult) },
        { D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM2, sizeof(BOOL),
            SVGA3D_QUERYTYPE_SOP_STREAM2,                    sizeof(SVGADXStreamOutPredicateQueryResult) },
        { D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3, sizeof(BOOL),
            SVGA3D_QUERYTYPE_SOP_STREAM3,                     sizeof(SVGADXStreamOutPredicateQueryResult) },
    };

    AssertReturn(Query < RT_ELEMENTS(aQueryInfo), NULL);
    return &aQueryInfo[Query];
}


#ifdef VBOX_STRICT
static bool isBeginDisabled(D3D10DDI_QUERY q)
{
    return q == D3D10DDI_QUERY_EVENT
        || q == D3D10DDI_QUERY_TIMESTAMP;
}
#endif


static HRESULT vboxdxCreateQueryInternal(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, D3D10DDI_QUERY Query, UINT MiscFlags)
{
    VMSVGAQUERYINFO const *pQueryInfo = getQueryInfo(Query);
    AssertReturn(pQueryInfo, E_INVALIDARG);

    pQuery->Query = Query;
    pQuery->svga.queryType = pQueryInfo->queryTypeSvga;
    pQuery->svga.flags = 0;
    if (MiscFlags & D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT)
        pQuery->svga.flags |= SVGA3D_DXQUERY_FLAG_PREDICATEHINT;
    pQuery->enmQueryState = VBOXDXQUERYSTATE_CREATED;
    pQuery->u64Value = 0;
    pQuery->EndRenderCbSequence = 0;

    int rc = RTHandleTableAlloc(pDevice->hHTQuery, pQuery, &pQuery->uQueryId);
    AssertRCReturn(rc, E_OUTOFMEMORY);

    /* Allocate mob space for this query. */
    pQuery->pCOAllocation = NULL;
    uint32_t const cbAlloc = (pQuery->Query != D3D10DDI_QUERY_EVENT ? sizeof(uint32_t) : 0) + pQueryInfo->cbDataSvga;
    PVBOXDXKMRESOURCE pIter;
    RTListForEach(&pDevice->listCOAQuery, pIter, VBOXDXKMRESOURCE, co.nodeAllocationsChain)
    {
        if (vboxDXCOABlockAlloc(pIter, cbAlloc, &pQuery->offQuery))
        {
            pQuery->pCOAllocation = pIter;
            break;
        }
    }

    if (!pQuery->pCOAllocation)
    {
        /* Create a new allocation.  */
        if (!vboxDXCreateCOAllocation(pDevice, &pDevice->listCOAQuery, &pQuery->pCOAllocation, 4 * _1K, /*fMap=*/ true))
            AssertFailedReturnStmt(RTHandleTableFree(pDevice->hHTQuery, pQuery->uQueryId), E_OUTOFMEMORY);

        if (!vboxDXCOABlockAlloc(pQuery->pCOAllocation, cbAlloc, &pQuery->offQuery))
            AssertFailedReturnStmt(RTHandleTableFree(pDevice->hHTQuery, pQuery->uQueryId), E_OUTOFMEMORY);
    }

    RTListAppend(&pDevice->listQueries, &pQuery->nodeQuery);

    if (pQuery->Query != D3D10DDI_QUERY_EVENT)
    {
        uint32_t *pu32QueryState = (uint32_t *)(pQuery->pCOAllocation->co.pu8COMapped + pQuery->offQuery);
        *pu32QueryState = SVGA3D_QUERYSTATE_PENDING;

        vgpu10DefineQuery(pDevice, pQuery->uQueryId, pQuery->svga.queryType, pQuery->svga.flags);
        vgpu10BindQuery(pDevice, pQuery->uQueryId, pQuery->pCOAllocation);
        vgpu10SetQueryOffset(pDevice, pQuery->uQueryId, pQuery->offQuery);
    }
    else
    {
        uint64_t *pu64Value = (uint64_t *)(pQuery->pCOAllocation->co.pu8COMapped + pQuery->offQuery);
        *pu64Value = 0;
    }

    return S_OK;
}


void vboxDXCreateQuery(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, D3D10DDI_QUERY Query, UINT MiscFlags)
{
    HRESULT hr = vboxdxCreateQueryInternal(pDevice, pQuery, Query, MiscFlags);
    if (hr != S_OK)
        vboxDXDeviceSetError(pDevice, hr);
}


void vboxDXDestroyQuery(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery)
{
    if (pQuery->Query != D3D10DDI_QUERY_EVENT)
        vgpu10DestroyQuery(pDevice, pQuery->uQueryId);

    if (pQuery->pCOAllocation)
    {
        vboxDXCOABlockFree(pQuery->pCOAllocation, pQuery->offQuery);
        pQuery->pCOAllocation = NULL;
    }

    RTListNodeRemove(&pQuery->nodeQuery);
    RTHandleTableFree(pDevice->hHTQuery, pQuery->uQueryId);
}


void vboxDXQueryBegin(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery)
{
    Assert(   pQuery->enmQueryState == VBOXDXQUERYSTATE_CREATED
           || pQuery->enmQueryState == VBOXDXQUERYSTATE_SIGNALED
           || ((pQuery->svga.flags & SVGA3D_DXQUERY_FLAG_PREDICATEHINT) && pQuery->enmQueryState == VBOXDXQUERYSTATE_ISSUED));

    pQuery->enmQueryState = VBOXDXQUERYSTATE_BUILDING;
    if (pQuery->Query == D3D10DDI_QUERY_EVENT)
        return;

    vgpu10BeginQuery(pDevice, pQuery->uQueryId);
}


void vboxDXQueryEnd(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery)
{
    Assert(   pQuery->enmQueryState == VBOXDXQUERYSTATE_BUILDING
           || (   isBeginDisabled(pQuery->Query)
               && (   pQuery->enmQueryState == VBOXDXQUERYSTATE_CREATED
                   || pQuery->enmQueryState == VBOXDXQUERYSTATE_SIGNALED)
              )
          );

    pQuery->enmQueryState = VBOXDXQUERYSTATE_ISSUED;
    pQuery->EndRenderCbSequence = pDevice->RenderCbSequence;

    if (pQuery->Query == D3D10DDI_QUERY_EVENT)
    {
        pQuery->u64Value = ASMAtomicIncU64(&pDevice->u64MobFenceValue);
        vgpu10MobFence64(pDevice, pQuery->u64Value, pQuery->pCOAllocation, pQuery->offQuery);
        return;
    }

    vgpu10EndQuery(pDevice, pQuery->uQueryId);
}


static HRESULT vboxdxQueryGetDataInternal(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, VOID* pData, UINT DataSize)
{
    HRESULT hr;

    Assert(pQuery->enmQueryState == VBOXDXQUERYSTATE_ISSUED || pQuery->enmQueryState == VBOXDXQUERYSTATE_SIGNALED);

    if (pQuery->Query == D3D10DDI_QUERY_EVENT)
    {
        uint64_t const *pu64Value = (uint64_t *)(pQuery->pCOAllocation->co.pu8COMapped + pQuery->offQuery);
        uint64_t const u64Value = *pu64Value;

        if (u64Value < pQuery->u64Value)
            hr = DXGI_DDI_ERR_WASSTILLDRAWING;
        else
        {
            pQuery->enmQueryState = VBOXDXQUERYSTATE_SIGNALED;

            if (pData && DataSize >= sizeof(BOOL))
                *(BOOL *)pData = TRUE;

            hr = S_OK;
        }
        return hr;
    }

    vgpu10ReadbackQuery(pDevice, pQuery->uQueryId);

    VMSVGAQUERYINFO const *pQueryInfo = getQueryInfo(pQuery->Query);
    AssertReturn(pQueryInfo, E_INVALIDARG);

    void *pvResult = RTMemTmpAlloc(pQueryInfo->cbDataSvga);
    AssertReturn(pvResult, E_OUTOFMEMORY);

    uint32_t u32QueryStatus = SVGA3D_QUERYSTATE_PENDING;

    uint8_t const *pu8 = pQuery->pCOAllocation->co.pu8COMapped + pQuery->offQuery;
    u32QueryStatus = *(uint32_t *)pu8;

    memcpy(pvResult, pu8 + sizeof(uint32_t), pQueryInfo->cbDataSvga);

    if (u32QueryStatus != SVGA3D_QUERYSTATE_SUCCEEDED)
        hr = DXGI_DDI_ERR_WASSTILLDRAWING;
    else
    {
        pQuery->enmQueryState = VBOXDXQUERYSTATE_SIGNALED;

        if (pData && DataSize >= pQueryInfo->cbDataDDI)
        {
            typedef union DDIQUERYRESULT
            {
                UINT64                                   occlusion;            /* D3D10DDI_QUERY_OCCLUSION */
                UINT64                                   timestamp;            /* D3D10DDI_QUERY_TIMESTAMP */
                D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT  timestampDisjoint;    /* D3D10DDI_QUERY_TIMESTAMPDISJOINT */
                D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS pipelineStatistics10; /* D3D10DDI_QUERY_PIPELINESTATS */
                BOOL                                     occlusionPredicate;   /* D3D10DDI_QUERY_OCCLUSIONPREDICATE */
                D3D10_DDI_QUERY_DATA_SO_STATISTICS       soStatistics;         /* D3D10DDI_QUERY_STREAMOUTPUTSTATS, D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM[0-3] */
                BOOL                                     soOverflowPredicate;  /* D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE, D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM[0-3] */
                D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS pipelineStatistics11; /* D3D11DDI_QUERY_PIPELINESTATS */
            } DDIQUERYRESULT;
            SVGADXQueryResultUnion const *pSvgaData = (SVGADXQueryResultUnion *)pvResult;
            DDIQUERYRESULT *pDDIData = (DDIQUERYRESULT *)pData;
            switch (pQuery->Query)
            {
                case D3D10DDI_QUERY_OCCLUSION:
                {
                    pDDIData->occlusion = pSvgaData->occ.samplesRendered;
                    break;
                }
                case D3D10DDI_QUERY_TIMESTAMP:
                {
                    pDDIData->timestamp = pSvgaData->ts.timestamp;
                    break;
                }
                case D3D10DDI_QUERY_TIMESTAMPDISJOINT:
                {
                    pDDIData->timestampDisjoint.Frequency = pSvgaData->tsDisjoint.realFrequency;
                    pDDIData->timestampDisjoint.Disjoint = pSvgaData->tsDisjoint.disjoint;
                    break;
                }
                case D3D10DDI_QUERY_PIPELINESTATS:
                {
                    pDDIData->pipelineStatistics10.IAVertices    = pSvgaData->pipelineStats.inputAssemblyVertices;
                    pDDIData->pipelineStatistics10.IAPrimitives  = pSvgaData->pipelineStats.inputAssemblyPrimitives;
                    pDDIData->pipelineStatistics10.VSInvocations = pSvgaData->pipelineStats.vertexShaderInvocations;
                    pDDIData->pipelineStatistics10.GSInvocations = pSvgaData->pipelineStats.geometryShaderInvocations;
                    pDDIData->pipelineStatistics10.GSPrimitives  = pSvgaData->pipelineStats.geometryShaderPrimitives;
                    pDDIData->pipelineStatistics10.CInvocations  = pSvgaData->pipelineStats.clipperInvocations;
                    pDDIData->pipelineStatistics10.CPrimitives   = pSvgaData->pipelineStats.clipperPrimitives;
                    pDDIData->pipelineStatistics10.PSInvocations = pSvgaData->pipelineStats.pixelShaderInvocations;
                    break;
                }
                case D3D10DDI_QUERY_OCCLUSIONPREDICATE:
                {
                    pDDIData->occlusionPredicate = pSvgaData->occPred.anySamplesRendered;
                    break;
                }
                case D3D10DDI_QUERY_STREAMOUTPUTSTATS:
                case D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0:
                case D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM1:
                case D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM2:
                case D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM3:
                {
                    pDDIData->soStatistics.NumPrimitivesWritten    = pSvgaData->soStats.numPrimitivesWritten;
                    pDDIData->soStatistics.PrimitivesStorageNeeded = pSvgaData->soStats.numPrimitivesRequired;
                    break;
                }
                case D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0:
                case D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM1:
                case D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM2:
                case D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3:
                case D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE:
                {
                    pDDIData->soOverflowPredicate = pSvgaData->soPred.overflowed;
                    break;
                }
                case D3D11DDI_QUERY_PIPELINESTATS:
                {
                    pDDIData->pipelineStatistics11.IAVertices    = pSvgaData->pipelineStats.inputAssemblyVertices;
                    pDDIData->pipelineStatistics11.IAPrimitives  = pSvgaData->pipelineStats.inputAssemblyPrimitives;
                    pDDIData->pipelineStatistics11.VSInvocations = pSvgaData->pipelineStats.vertexShaderInvocations;
                    pDDIData->pipelineStatistics11.GSInvocations = pSvgaData->pipelineStats.geometryShaderInvocations;
                    pDDIData->pipelineStatistics11.GSPrimitives  = pSvgaData->pipelineStats.geometryShaderPrimitives;
                    pDDIData->pipelineStatistics11.CInvocations  = pSvgaData->pipelineStats.clipperInvocations;
                    pDDIData->pipelineStatistics11.CPrimitives   = pSvgaData->pipelineStats.clipperPrimitives;
                    pDDIData->pipelineStatistics11.PSInvocations = pSvgaData->pipelineStats.pixelShaderInvocations;
                    pDDIData->pipelineStatistics11.HSInvocations = pSvgaData->pipelineStats.hullShaderInvocations;
                    pDDIData->pipelineStatistics11.DSInvocations = pSvgaData->pipelineStats.domainShaderInvocations;
                    pDDIData->pipelineStatistics11.CSInvocations = pSvgaData->pipelineStats.computeShaderInvocations;
                    break;
                }
                default:
                    break;
            }
        }

        hr = S_OK;
    }

    RTMemTmpFree(pvResult);
    return hr;
}


void vboxDXQueryGetData(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, VOID* pData, UINT DataSize, UINT Flags)
{
    if (   !RT_BOOL(Flags & D3D10_DDI_GET_DATA_DO_NOT_FLUSH)
        && pQuery->EndRenderCbSequence == pDevice->RenderCbSequence)
        vboxDXDeviceFlushCommands(pDevice);

    HRESULT hr = vboxdxQueryGetDataInternal(pDevice, pQuery, pData, DataSize);
    if (hr != S_OK)
        vboxDXDeviceSetError(pDevice, hr);
}


void vboxDXSetPredication(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, BOOL PredicateValue)
{
    vgpu10SetPredication(pDevice, pQuery ? pQuery->uQueryId : SVGA3D_INVALID_ID, PredicateValue);
}


void vboxDXSetShader(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, PVBOXDXSHADER pShader)
{
    if (enmShaderType == SVGA3D_SHADERTYPE_GS)
        vgpu10SetStreamOutput(pDevice, pShader ? pShader->gs.uStreamOutputId : SVGA3D_INVALID_ID);
    vgpu10SetShader(pDevice, pShader ? pShader->uShaderId : SVGA3D_INVALID_ID, enmShaderType);
}


void vboxDXSetVertexBuffers(PVBOXDX_DEVICE pDevice, UINT StartSlot, UINT NumBuffers,
                            PVBOXDX_RESOURCE *papBuffers, const UINT *pStrides, const UINT *pOffsets)
{
    AssertReturnVoidStmt(  StartSlot < SVGA3D_MAX_VERTEX_ARRAYS
                         && NumBuffers <= SVGA3D_MAX_VERTEX_ARRAYS
                         && StartSlot + NumBuffers <= SVGA3D_MAX_VERTEX_ARRAYS,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    /* Remember which buffers must be set. The buffers will be actually set right before a draw call,
     * because this allows the updates of the buffers content to be done prior to setting the buffers on the host.
     */
    PVBOXDXVERTEXBUFFERSSTATE pVBS = &pDevice->pipeline.VertexBuffers;

    for (unsigned i = 0; i < NumBuffers; ++i)
    {
        pVBS->apResource[StartSlot + i] = papBuffers[i];
        pVBS->aStrides[StartSlot + i] = pStrides[i];
        pVBS->aOffsets[StartSlot + i] = pOffsets[i];
        LogFunc(("slot %d, stride %d, offset %d",
                 StartSlot + i, pVBS->aStrides[StartSlot + i], pVBS->aOffsets[StartSlot + i]));
    }

    /* Join the current range and the new range. */
    if (pVBS->NumBuffers == 0)
    {
        pVBS->StartSlot = StartSlot;
        pVBS->NumBuffers = NumBuffers;
    }
    else
    {
        UINT FirstSlot = RT_MIN(StartSlot, pVBS->StartSlot);
        UINT EndSlot = RT_MAX(pVBS->StartSlot + pVBS->NumBuffers, StartSlot + NumBuffers);
        pVBS->StartSlot = FirstSlot;
        pVBS->NumBuffers = EndSlot - FirstSlot;
    }
}


void vboxDXSetIndexBuffer(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pBuffer, DXGI_FORMAT Format, UINT Offset)
{
    PVBOXDXINDEXBUFFERSTATE pIBS = &pDevice->pipeline.IndexBuffer;
    pIBS->pBuffer = pBuffer;
    pIBS->Format = Format;
    pIBS->Offset = Offset;
}


void vboxDXSoSetTargets(PVBOXDX_DEVICE pDevice, uint32_t NumTargets,
                        PVBOXDXKMRESOURCE *papKMResources, uint32_t *paOffsets, uint32_t *paSizes)
{
    vgpu10SoSetTargets(pDevice, NumTargets, papKMResources, paOffsets, paSizes);
}


static bool vboxDXDynamicOrStagingUpdateUP(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource,
                                           UINT DstSubresource, const D3D10_DDI_BOX *pDstBox,
                                           const VOID *pSysMemUP, UINT RowPitch, UINT DepthPitch, UINT CopyFlags)
{
    RT_NOREF(CopyFlags);
    AssertReturnStmt(   pDstResource->Usage == D3D10_DDI_USAGE_DYNAMIC
                     || pDstResource->Usage == D3D10_DDI_USAGE_STAGING,
                     vboxDXDeviceSetError(pDevice, E_INVALIDARG), false);

    VBOXDXALLOCATIONDESC const *pDstAllocationDesc = vboxDXGetAllocationDesc(pDstResource);
    AssertReturn(pDstAllocationDesc, false);

    SVGA3dBox destBox;
    if (pDstBox)
    {
        destBox.x = pDstBox->left;
        destBox.y = pDstBox->top;
        destBox.z = pDstBox->front;
        destBox.w = pDstBox->right - pDstBox->left;
        destBox.h = pDstBox->bottom - pDstBox->top;
        destBox.d = pDstBox->back - pDstBox->front;
    }
    else
    {
        vboxDXGetSubresourceBox(pDstAllocationDesc, DstSubresource, &destBox);
    }

    uint32_t offPixel;
    uint32_t cbRow;
    uint32_t cRows;
    uint32_t Depth;
    vboxDXGetResourceBoxDimensions(pDstAllocationDesc, DstSubresource, &destBox, &offPixel, &cbRow, &cRows, &Depth);

    UINT DstRowPitch;
    UINT DstDepthPitch;
    vboxDXGetSubresourcePitch(pDstAllocationDesc, DstSubresource, &DstRowPitch, &DstDepthPitch);

    /* The allocation contains all subresources, so get subresource offset too. */
    offPixel += vboxDXGetSubresourceOffset(pDstAllocationDesc, DstSubresource);
    //uint32_t const cbSubresource = vboxDXGetSubresourceSize(pDstAllocationDesc, DstSubresource);

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = vboxDXGetAllocation(pDstResource);
    ddiLock.Flags.WriteOnly = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        for (unsigned z = 0; z < Depth; ++z)
        {
            uint8_t *pu8Dst = (uint8_t *)ddiLock.pData + offPixel + z * DstDepthPitch;
            uint8_t const *pu8Src = (uint8_t *)pSysMemUP + z * DepthPitch;
            for (unsigned y = 0; y < cRows; ++y)
            {
                memcpy(pu8Dst, pu8Src, cbRow);
                pu8Dst += DstRowPitch;
                pu8Src += RowPitch;
            }
        }

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &ddiLock.hAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        if (SUCCEEDED(hr))
        {
            /* Inform the host that the resource has been updated. */
            SVGA3dBox box;
            vboxDXGetSubresourceBox(pDstAllocationDesc, DstSubresource, &box);
            vgpu10UpdateSubResource(pDevice, vboxDXGetKMResource(pDstResource), DstSubresource, &box);
            return true;
        }
    }
    vboxDXDeviceSetError(pDevice, hr);
    return false;
}


static bool vboxDXUpdateStagingBufferUP(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pBuffer,
                                        UINT offDstPixel, UINT cbRow, UINT cRows, UINT DstRowPitch, UINT Depth, UINT DstDepthPitch,
                                        const VOID *pSysMemUP, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = vboxDXGetAllocation(pBuffer);
    ddiLock.Flags.WriteOnly = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        /* Placement of the data in the destination buffer is the same as in the surface. */
        for (unsigned z = 0; z < Depth; ++z)
        {
            uint8_t *pu8Dst = (uint8_t *)ddiLock.pData + offDstPixel + z * DstDepthPitch;
            uint8_t const *pu8Src = (uint8_t *)pSysMemUP + z * SrcDepthPitch;
            for (unsigned y = 0; y < cRows; ++y)
            {
                memcpy(pu8Dst, pu8Src, cbRow);
                pu8Dst += DstRowPitch;
                pu8Src += SrcRowPitch;
            }
        }

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &ddiLock.hAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        if (SUCCEEDED(hr))
            return true;
    }
    vboxDXDeviceSetError(pDevice, hr);
    return false;
}


static PVBOXDX_RESOURCE vboxDXCreateStagingBuffer(PVBOXDX_DEVICE pDevice, UINT cbAllocation)
{
    PVBOXDX_RESOURCE pStagingResource = (PVBOXDX_RESOURCE)RTMemAlloc(sizeof(VBOXDX_RESOURCE)
                                                                     + 1 * sizeof(D3D10DDI_MIPINFO));
    AssertReturnStmt(pStagingResource, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), false);

    D3D11DDIARG_CREATERESOURCE createResource;
    D3D10DDI_MIPINFO mipInfo;

    mipInfo.TexelWidth     = cbAllocation;
    mipInfo.TexelHeight    = 1;
    mipInfo.TexelDepth     = 1;
    mipInfo.PhysicalWidth  = mipInfo.TexelWidth;
    mipInfo.PhysicalHeight = mipInfo.TexelHeight;
    mipInfo.PhysicalDepth  = mipInfo.TexelDepth;

    createResource.pMipInfoList       = &mipInfo;
    createResource.pInitialDataUP     = 0;
    createResource.ResourceDimension  = D3D10DDIRESOURCE_BUFFER;
    createResource.Usage              = D3D10_DDI_USAGE_STAGING;
    createResource.BindFlags          = 0;
    createResource.MapFlags           = D3D10_DDI_CPU_ACCESS_WRITE;
    createResource.MiscFlags          = 0;
    createResource.Format             = DXGI_FORMAT_UNKNOWN;
    createResource.SampleDesc.Count   = 0;
    createResource.SampleDesc.Quality = 0;
    createResource.MipLevels          = 1;
    createResource.ArraySize          = 1;
    createResource.pPrimaryDesc       = 0;
    createResource.ByteStride         = 0;
    createResource.DecoderBufferType  = D3D11_1DDI_VIDEO_DECODER_BUFFER_UNKNOWN;
    createResource.TextureLayout      = D3DWDDM2_0DDI_TL_UNDEFINED;

    pStagingResource->hRTResource.handle = 0; /* This resource has not been created by D3D runtime. */
    if (vboxDXCreateResource(pDevice, pStagingResource, &createResource))
        return pStagingResource;

    RTMemFree(pStagingResource);
    vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY);
    return NULL;
}


static bool dxUploadViaBuffer(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource,
                              UINT DstSubresource, const SVGA3dBox &destBox,
                              const VOID *pSysMemUP, UINT RowPitch, UINT DepthPitch, UINT CopyFlags)
{
    RT_NOREF(CopyFlags);

    VBOXDXALLOCATIONDESC const *pBufferAllocationDesc = vboxDXGetAllocationDesc(pDevice->upload.pUploadBuffer);
    VBOXDXALLOCATIONDESC const *pDstAllocationDesc = vboxDXGetAllocationDesc(pDstResource);

    /* Calculate size of destBox in bytes. */
    VBOXDXRESOURCEBOXDESC boxDesc;
    vboxDXGetResourceBoxDesc(pDstAllocationDesc, destBox, &boxDesc);

    uint32_t const cbTransfer = boxDesc.cbBox;

    if (cbTransfer > pDevice->upload.cbFree)
        return false;

    uint32_t offTransfer;
    uint32_t cbConsumed = cbTransfer;

    /* See if this amount of bytes can be placed contiguously in the upload ring buffer. */
    uint32_t const cbBeforeBoundary = pBufferAllocationDesc->cbAllocation - pDevice->upload.offFree;

    if (cbBeforeBoundary >= pDevice->upload.cbFree)
    {
        /* The free space is one chunk which does not cross the buffer boundary. */
        if (cbTransfer <= pDevice->upload.cbFree)
            offTransfer = pDevice->upload.offFree;
        else
            return false;
    }
    else
    {
        /* Two free chunks: before boundary and from offset 0. */
        if (cbTransfer <= cbBeforeBoundary)
            offTransfer = pDevice->upload.offFree;
        else
        {
            /* Check the second chunk. */
            if (cbTransfer <= pDevice->upload.cbFree - cbBeforeBoundary)
            {
                cbConsumed += cbBeforeBoundary;
                offTransfer = 0;
            }
            else
                return false;
        }
    }

    Assert(   pBufferAllocationDesc->cbAllocation > offTransfer
           && pBufferAllocationDesc->cbAllocation - offTransfer >= cbTransfer);

    /* Copy data to the ring buffer.
     * Scanlines of the destination box data are tightly packed in the buffer. */
    uint8_t *pu8Dst = (uint8_t *)pDevice->upload.pvUploadBufferMapped + offTransfer;

    if (RT_LIKELY(boxDesc.cbRowPitch > 0))
    {
        for (unsigned z = 0; z < boxDesc.czBlocks; ++z)
        {
            uint8_t const *pu8Src = (uint8_t *)pSysMemUP + z * DepthPitch;
            for (unsigned y = 0; y < boxDesc.cyBlocks; ++y)
            {
                memcpy(pu8Dst, pu8Src, boxDesc.cbRowPitch);
                pu8Src += RowPitch;
                pu8Dst += boxDesc.cbRowPitch;
            }
        }
    }
    else
    {
        /* Planar format. */
        uint8_t const *pu8Src = (uint8_t *)pSysMemUP;
        memcpy(pu8Dst, pu8Src, boxDesc.cbBox);
    }

    /* Update the free space description. */
    pDevice->upload.offFree = (pDevice->upload.offFree + cbConsumed) % VBOXDX_UPLOAD_BUFFER_SIZE;
    pDevice->upload.cbFree -= cbConsumed;

    /* Inform the host that the upload buffer has been updated. */
    SVGA3dBox box;
    box.x = offTransfer;
    box.y = 0;
    box.z = 0;
    box.w = cbTransfer;
    box.h = 1;
    box.d = 1;
    vgpu10UpdateSubResource(pDevice, vboxDXGetKMResource(pDevice->upload.pUploadBuffer), 0, &box);

    /* Issue SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER */
    uint32 const srcOffset = offTransfer;
    uint32 const srcPitch = boxDesc.cbRowPitch;
    uint32 const srcSlicePitch = boxDesc.cbDepthPitch;
    vgpu10TransferFromBuffer(pDevice,
                             vboxDXGetKMResource(pDevice->upload.pUploadBuffer), srcOffset, srcPitch, srcSlicePitch,
                             vboxDXGetKMResource(pDstResource), DstSubresource, destBox);

    Assert(pDevice->upload.pCurrentBatch);
    Assert(pDevice->upload.pCurrentBatch->cbBatch <= pBufferAllocationDesc->cbAllocation - cbConsumed);
    pDevice->upload.pCurrentBatch->cbBatch += cbConsumed;

    return true;
}


void vboxDXResourceUpdateSubresourceUP(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource,
                                       UINT DstSubresource, const D3D10_DDI_BOX *pDstBox,
                                       const VOID *pSysMemUP, UINT RowPitch, UINT DepthPitch, UINT CopyFlags)
{
    if (   pDstResource->Usage != D3D10_DDI_USAGE_DEFAULT
        && pDstResource->Usage != D3D10_DDI_USAGE_IMMUTABLE)
    {
        vboxDXDynamicOrStagingUpdateUP(pDevice, pDstResource, DstSubresource, pDstBox,
                                       pSysMemUP, RowPitch, DepthPitch, CopyFlags);
        return;
    }

    /* DEFAULT resources are updated via a staging buffer. */

    VBOXDXALLOCATIONDESC const *pDstAllocationDesc = vboxDXGetAllocationDesc(pDstResource);
    AssertReturnVoidStmt(pDstAllocationDesc, vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    SVGA3dBox destBox;
    if (pDstBox)
    {
        destBox.x = pDstBox->left;
        destBox.y = pDstBox->top;
        destBox.z = pDstBox->front;
        destBox.w = pDstBox->right - pDstBox->left;
        destBox.h = pDstBox->bottom - pDstBox->top;
        destBox.d = pDstBox->back - pDstBox->front;
    }
    else
        vboxDXGetSubresourceBox(pDstAllocationDesc, DstSubresource, &destBox);

    /*
     * Try to use the upload buffer.
     */
    if (dxUploadViaBuffer(pDevice, pDstResource, DstSubresource, destBox,
                          pSysMemUP, RowPitch, DepthPitch, CopyFlags))
        return;

    /*
     * Fallback: allocate a staging buffer for the upload and delete the buffers after a flush.
     */

    /*
     * Allocate a staging buffer big enough to hold the entire subresource.
     */
    uint32_t const cbStagingBuffer = vboxDXGetSubresourceSize(pDstAllocationDesc, DstSubresource);
    PVBOXDX_RESOURCE pStagingBuffer = vboxDXCreateStagingBuffer(pDevice, cbStagingBuffer);
    if (!pStagingBuffer)
        return;

    /*
     * Copy data to staging via map/unmap.
     */

    uint32_t offPixel;
    uint32_t cbRow;
    UINT cRows;
    UINT Depth;
    vboxDXGetResourceBoxDimensions(pDstAllocationDesc, DstSubresource, &destBox, &offPixel, &cbRow, &cRows, &Depth);

    UINT cbRowPitch;
    UINT cbDepthPitch;
    vboxDXGetSubresourcePitch(pDstAllocationDesc, DstSubresource, &cbRowPitch, &cbDepthPitch);

    if (!vboxDXUpdateStagingBufferUP(pDevice, pStagingBuffer,
                                     offPixel, cbRow, cRows, cbRowPitch, Depth, cbDepthPitch,
                                     pSysMemUP, RowPitch, DepthPitch))
        return;

    /*
     * Copy from staging to destination.
     */
    /* Inform the host that the staging buffer has been updated. Part occupied by the DstSubresource. */
    SVGA3dBox box;
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = cbStagingBuffer;
    box.h = 1;
    box.d = 1;
    vgpu10UpdateSubResource(pDevice, vboxDXGetKMResource(pStagingBuffer), 0, &box);

    /* Issue SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER */
    uint32 srcOffset = offPixel;
    uint32 srcPitch = cbRowPitch;
    uint32 srcSlicePitch = cbDepthPitch;
    vgpu10TransferFromBuffer(pDevice, vboxDXGetKMResource(pStagingBuffer), srcOffset, srcPitch, srcSlicePitch,
                             vboxDXGetKMResource(pDstResource), DstSubresource, destBox);

    RTListPrepend(&pDevice->listStagingResources, &pStagingBuffer->pKMResource->resource.nodeStaging);
}

#ifndef D3DERR_WASSTILLDRAWING
#define D3DERR_WASSTILLDRAWING 0x8876021c
#endif

void vboxDXResourceMap(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT Subresource,
                       D3D10_DDI_MAP DDIMap, UINT Flags, D3D10DDI_MAPPED_SUBRESOURCE *pMappedSubResource)
{
    D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pResource);
    AssertReturnVoidStmt(hAllocation, vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    /** @todo Need to take into account various variants Dynamic/Staging/ Discard/NoOverwrite, etc. */
    Assert(pResource->uMap == 0); /* Must not be already mapped */

    if (dxIsResourceInUse(pDevice, pResource->pKMResource) && DDIMap != D3D10_DDI_MAP_WRITE_NOOVERWRITE)
    {
        vboxDXFlush(pDevice, true);

        if (RT_BOOL(Flags & D3D10_DDI_MAP_FLAG_DONOTWAIT))
        {
            vboxDXDeviceSetError(pDevice, DXGI_DDI_ERR_WASSTILLDRAWING);
            return;
        }
    }

    /* Readback for read access. */
    if (DDIMap == D3D10_DDI_MAP_READ || DDIMap == D3D10_DDI_MAP_READWRITE)
    {
        vgpu10ReadbackSubResource(pDevice, vboxDXGetKMResource(pResource), Subresource);
        vboxDXFlush(pDevice, true);
        /* DXGK now knows that the allocation is in use. So pfnLockCb waits until the data is ready. */
    }

    HRESULT hr;
    D3DDDICB_LOCK ddiLock;
    do
    {
        RT_ZERO(ddiLock);
        ddiLock.hAllocation = hAllocation;
        ddiLock.Flags.ReadOnly =   DDIMap == D3D10_DDI_MAP_READ;
        ddiLock.Flags.WriteOnly =  DDIMap == D3D10_DDI_MAP_WRITE
                                || DDIMap == D3D10_DDI_MAP_WRITE_DISCARD
                                || DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE;
        ddiLock.Flags.DonotWait = RT_BOOL(Flags & D3D10_DDI_MAP_FLAG_DONOTWAIT);
        if (DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE)
        {
            ddiLock.Flags.IgnoreSync = 1;
            ddiLock.Flags.DonotWait = 1;
        }
        ddiLock.Flags.Discard = DDIMap == D3D10_DDI_MAP_WRITE_DISCARD;
        /** @todo Other flags? */

        hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
        if (hr == D3DERR_WASSTILLDRAWING)
        {
            if (RT_BOOL(Flags & D3D10_DDI_MAP_FLAG_DONOTWAIT))
            {
                vboxDXDeviceSetError(pDevice, DXGI_DDI_ERR_WASSTILLDRAWING);
                return;
            }

            RTThreadYield();
        }
    } while (hr == D3DERR_WASSTILLDRAWING);

    if (SUCCEEDED(hr))
    {
        /* "If the Discard bit-field flag is set in the Flags member, the video memory manager creates
         * a new instance of the allocation and returns a new handle that represents the new instance."
         */
        if (   DDIMap == D3D10_DDI_MAP_WRITE_DISCARD
            && pResource->pKMResource->hAllocation != ddiLock.hAllocation)
        {
            pResource->pKMResource->hAllocation = ddiLock.hAllocation;
            vgpu10BindGBSurface(pDevice, vboxDXGetKMResource(pResource));
        }

        VBOXDXALLOCATIONDESC const *pAllocationDesc = vboxDXGetAllocationDesc(pResource);

        uint32_t const offSubresource = vboxDXGetSubresourceOffset(pAllocationDesc, Subresource);
        pMappedSubResource->pData = (uint8_t *)ddiLock.pData + offSubresource;
        vboxDXGetSubresourcePitch(pAllocationDesc, Subresource, &pMappedSubResource->RowPitch, &pMappedSubResource->DepthPitch);

        pResource->DDIMap = DDIMap;
    }
    else
        vboxDXDeviceSetError(pDevice, hr);
}


void vboxDXResourceUnmap(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT Subresource)
{
    D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pResource);
    AssertReturnVoidStmt(hAllocation, vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    D3DDDICB_UNLOCK ddiUnlock;
    ddiUnlock.NumAllocations = 1;
    ddiUnlock.phAllocations = &hAllocation;
    HRESULT hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
    if (SUCCEEDED(hr))
    {
        if (   pResource->DDIMap == D3D10_DDI_MAP_WRITE
            || pResource->DDIMap == D3D10_DDI_MAP_READWRITE
            || pResource->DDIMap == D3D10_DDI_MAP_WRITE_DISCARD
            || pResource->DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE)
        {
            /* Inform the host that the resource has been updated. */
            SVGA3dBox box;
            vboxDXGetSubresourceBox(vboxDXGetAllocationDesc(pResource), Subresource, &box);
            vgpu10UpdateSubResource(pDevice, vboxDXGetKMResource(pResource), Subresource, &box);
        }

        pResource->uMap = 0;
    }
    else
        vboxDXDeviceSetError(pDevice, hr);
}


void vboxDXDynamicConstantBufferMapDiscard(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT Flags,
                                           D3D10DDI_MAPPED_SUBRESOURCE *pMappedSubResource)
{
    VBOXDXALLOCATIONDESC const *pDstAllocationDesc = vboxDXGetAllocationDesc(pResource);
    AssertReturnVoidStmt(pDstAllocationDesc, vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    VBOXDXTRANSIENTHEAP *pHeap = &pDevice->transientHeap;
    uint32_t const cbBlock = pDstAllocationDesc->cbAllocation;
    VBOXDXTRANSIENTHEAPBLOCKDESC *pBlock = dxTransientBlockReserve(pHeap, cbBlock);
    if (!pBlock)
    {
        vboxDXResourceMap(pDevice, pResource, 0, D3D10_DDI_MAP_WRITE_DISCARD, Flags, pMappedSubResource);
        return;
    }

    pMappedSubResource->pData = (uint8_t *)pHeap->pvTransientHeapMapped + pBlock->offBlock;
    pMappedSubResource->RowPitch = cbBlock;
    pMappedSubResource->DepthPitch = cbBlock;

    pResource->pMappedTransientHeapBlock = pBlock;
    pResource->DDIMap = D3D10_DDI_MAP_WRITE_DISCARD;
}


void vboxDXDynamicConstantBufferUnmap(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource)
{
    VBOXDXTRANSIENTHEAPBLOCKDESC *pBlock = pResource->pMappedTransientHeapBlock;
    if (pBlock == NULL)
    {
        /* The buffer was mapped via ddi10DynamicConstantBufferMapNoOverwrite
         * or the transient heap had no space for the buffer.
         */
        Assert(pResource->DDIMap == D3D10_DDI_MAP_WRITE_DISCARD);
        vboxDXResourceUnmap(pDevice, pResource, 0);
        return;
    }

    uint32 const srcOffset = pBlock->offBlock;
    uint32 const srcPitch = pBlock->cbBlock;

    /* Inform the host that the heap buffer has been updated. */
    SVGA3dBox box;
    box.x = srcOffset;
    box.y = 0;
    box.z = 0;
    box.w = srcPitch;
    box.h = 1;
    box.d = 1;
    vgpu10UpdateSubResource(pDevice, vboxDXGetKMResource(pBlock->pHeap->pTransientHeapBuffer), 0, &box);

    /* Issue SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER */
    SVGA3dBox destBox;
    destBox.x = 0;
    destBox.y = 0;
    destBox.z = 0;
    destBox.w = srcPitch;
    destBox.h = 1;
    destBox.d = 1;
    vgpu10TransferFromBuffer(pDevice,
                             vboxDXGetKMResource(pBlock->pHeap->pTransientHeapBuffer), srcOffset, srcPitch, srcPitch,
                             vboxDXGetKMResource(pResource), 0, destBox);

    Assert(pBlock->nodeTransientBlock.pNext == NULL);
    RTListAppend(&pBlock->pHeap->pCurrentBatch->listPendingBlocks, &pBlock->nodeTransientBlock);

    pResource->pMappedTransientHeapBlock = NULL;
    pResource->uMap = 0;
}


static SVGA3dResourceType d3dToSvgaResourceDimension(D3D10DDIRESOURCE_TYPE ResourceDimension)
{
    switch (ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:      return SVGA3D_RESOURCE_BUFFER;
        case D3D10DDIRESOURCE_TEXTURE1D:   return SVGA3D_RESOURCE_TEXTURE1D;
        case D3D10DDIRESOURCE_TEXTURE2D:   return SVGA3D_RESOURCE_TEXTURE2D;
        case D3D10DDIRESOURCE_TEXTURE3D:   return SVGA3D_RESOURCE_TEXTURE3D;
        case D3D10DDIRESOURCE_TEXTURECUBE: return SVGA3D_RESOURCE_TEXTURECUBE;
        case D3D11DDIRESOURCE_BUFFEREX:    return SVGA3D_RESOURCE_BUFFEREX;
    }
    AssertFailed();
    return D3D10DDIRESOURCE_BUFFER;
}


void vboxDXCreateShaderResourceView(PVBOXDX_DEVICE pDevice, PVBOXDXSHADERRESOURCEVIEW pShaderResourceView)
{
    int rc = RTHandleTableAlloc(pDevice->hHTShaderResourceView, pShaderResourceView, &pShaderResourceView->uShaderResourceViewId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    pShaderResourceView->svga.format            = vboxDXDxgiToSvgaFormat(pShaderResourceView->Format);
    pShaderResourceView->svga.resourceDimension = d3dToSvgaResourceDimension(pShaderResourceView->ResourceDimension);
    SVGA3dShaderResourceViewDesc *pDesc         = &pShaderResourceView->svga.desc;
    RT_ZERO(*pDesc);
    switch (pShaderResourceView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pDesc->buffer.firstElement   = pShaderResourceView->DimensionDesc.Buffer.FirstElement;
            pDesc->buffer.numElements    = pShaderResourceView->DimensionDesc.Buffer.NumElements;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDesc->tex.mostDetailedMip   = pShaderResourceView->DimensionDesc.Tex1D.MostDetailedMip;
            pDesc->tex.firstArraySlice   = pShaderResourceView->DimensionDesc.Tex1D.FirstArraySlice;
            pDesc->tex.mipLevels         = pShaderResourceView->DimensionDesc.Tex1D.MipLevels;
            pDesc->tex.arraySize         = pShaderResourceView->DimensionDesc.Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDesc->tex.mostDetailedMip   = pShaderResourceView->DimensionDesc.Tex2D.MostDetailedMip;
            pDesc->tex.firstArraySlice   = pShaderResourceView->DimensionDesc.Tex2D.FirstArraySlice;
            pDesc->tex.mipLevels         = pShaderResourceView->DimensionDesc.Tex2D.MipLevels;
            pDesc->tex.arraySize         = pShaderResourceView->DimensionDesc.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pDesc->tex.mostDetailedMip   = pShaderResourceView->DimensionDesc.Tex3D.MostDetailedMip;
            pDesc->tex.firstArraySlice   = 0;
            pDesc->tex.mipLevels         = pShaderResourceView->DimensionDesc.Tex3D.MipLevels;
            pDesc->tex.arraySize         = 0;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pDesc->tex.mostDetailedMip   = pShaderResourceView->DimensionDesc.TexCube.MostDetailedMip;
            pDesc->tex.firstArraySlice   = pShaderResourceView->DimensionDesc.TexCube.First2DArrayFace;
            pDesc->tex.mipLevels         = pShaderResourceView->DimensionDesc.TexCube.MipLevels;
            pDesc->tex.arraySize         = pShaderResourceView->DimensionDesc.TexCube.NumCubes;
            break;
        case D3D11DDIRESOURCE_BUFFEREX:
            pDesc->bufferex.firstElement = pShaderResourceView->DimensionDesc.BufferEx.FirstElement;;
            pDesc->bufferex.numElements  = pShaderResourceView->DimensionDesc.BufferEx.NumElements;
            pDesc->bufferex.flags        = pShaderResourceView->DimensionDesc.BufferEx.Flags;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            RTHandleTableFree(pDevice->hHTShaderResourceView, pShaderResourceView->uShaderResourceViewId);
            return;
    }

    vgpu10DefineShaderResourceView(pDevice, pShaderResourceView->uShaderResourceViewId, vboxDXGetKMResource(pShaderResourceView->pResource),
                                   pShaderResourceView->svga.format, pShaderResourceView->svga.resourceDimension,
                                   &pShaderResourceView->svga.desc);

    pShaderResourceView->fDefined = true;
    RTListAppend(&pShaderResourceView->pResource->listSRV, &pShaderResourceView->nodeView);
}


void vboxDXGenMips(PVBOXDX_DEVICE pDevice, PVBOXDXSHADERRESOURCEVIEW pShaderResourceView)
{
    vgpu10GenMips(pDevice, pShaderResourceView->uShaderResourceViewId,
                  vboxDXGetKMResource(pShaderResourceView->pResource));
}


void vboxDXDestroyShaderResourceView(PVBOXDX_DEVICE pDevice, PVBOXDXSHADERRESOURCEVIEW pShaderResourceView)
{
    RTListNodeRemove(&pShaderResourceView->nodeView);

    vgpu10DestroyShaderResourceView(pDevice, pShaderResourceView->uShaderResourceViewId,
                                    vboxDXGetKMResource(pShaderResourceView->pResource));
    RTHandleTableFree(pDevice->hHTShaderResourceView, pShaderResourceView->uShaderResourceViewId);
}


void vboxDXCreateRenderTargetView(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView)
{
    int rc = RTHandleTableAlloc(pDevice->hHTRenderTargetView, pRenderTargetView, &pRenderTargetView->uRenderTargetViewId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    pRenderTargetView->svga.format            = vboxDXDxgiToSvgaFormat(pRenderTargetView->Format);
    pRenderTargetView->svga.resourceDimension = d3dToSvgaResourceDimension(pRenderTargetView->ResourceDimension);
    SVGA3dRenderTargetViewDesc *pDesc         = &pRenderTargetView->svga.desc;
    RT_ZERO(*pDesc);
    switch (pRenderTargetView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pDesc->buffer.firstElement = pRenderTargetView->DimensionDesc.Buffer.FirstElement;
            pDesc->buffer.numElements  = pRenderTargetView->DimensionDesc.Buffer.NumElements;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDesc->tex.mipSlice        = pRenderTargetView->DimensionDesc.Tex1D.MipSlice;
            pDesc->tex.firstArraySlice = pRenderTargetView->DimensionDesc.Tex1D.FirstArraySlice;
            pDesc->tex.arraySize       = pRenderTargetView->DimensionDesc.Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDesc->tex.mipSlice        = pRenderTargetView->DimensionDesc.Tex2D.MipSlice;
            pDesc->tex.firstArraySlice = pRenderTargetView->DimensionDesc.Tex2D.FirstArraySlice;
            pDesc->tex.arraySize       = pRenderTargetView->DimensionDesc.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pDesc->tex3D.mipSlice      = pRenderTargetView->DimensionDesc.Tex3D.MipSlice;
            pDesc->tex3D.firstW        = pRenderTargetView->DimensionDesc.Tex3D.FirstW;
            pDesc->tex3D.wSize         = pRenderTargetView->DimensionDesc.Tex3D.WSize;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pDesc->tex.mipSlice        = pRenderTargetView->DimensionDesc.TexCube.MipSlice;
            pDesc->tex.firstArraySlice = pRenderTargetView->DimensionDesc.TexCube.FirstArraySlice;
            pDesc->tex.arraySize       = pRenderTargetView->DimensionDesc.TexCube.ArraySize;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            RTHandleTableFree(pDevice->hHTRenderTargetView, pRenderTargetView->uRenderTargetViewId);
            return;
    }

    vgpu10DefineRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId, vboxDXGetKMResource(pRenderTargetView->pResource),
                                 pRenderTargetView->svga.format, pRenderTargetView->svga.resourceDimension,
                                 &pRenderTargetView->svga.desc);

    pRenderTargetView->fDefined = true;
    RTListAppend(&pRenderTargetView->pResource->listRTV, &pRenderTargetView->nodeView);
}


void vboxDXClearRenderTargetView(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView, const FLOAT ColorRGBA[4])
{
    vgpu10ClearRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId,
                                vboxDXGetKMResource(pRenderTargetView->pResource), ColorRGBA);
}


void vboxDXClearRenderTargetViewRegion(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView, const FLOAT Color[4], const D3D10_DDI_RECT *pRect, UINT NumRects)
{
    vgpu10ClearRenderTargetViewRegion(pDevice, pRenderTargetView->uRenderTargetViewId,
                                      vboxDXGetKMResource(pRenderTargetView->pResource), Color, pRect, NumRects);
}


void vboxDXDestroyRenderTargetView(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView)
{
    for (unsigned i = 0; i < pDevice->pipeline.cRenderTargetViews; ++i)
    {
        if (pDevice->pipeline.apRenderTargetViews[i] == pRenderTargetView)
        {
            DEBUG_BREAKPOINT_TEST();
        }
    }

    RTListNodeRemove(&pRenderTargetView->nodeView);

    vgpu10DestroyRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId,
                                  vboxDXGetKMResource(pRenderTargetView->pResource));
    RTHandleTableFree(pDevice->hHTRenderTargetView, pRenderTargetView->uRenderTargetViewId);
}


void vboxDXCreateDepthStencilView(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView)
{
    int rc = RTHandleTableAlloc(pDevice->hHTDepthStencilView, pDepthStencilView, &pDepthStencilView->uDepthStencilViewId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    pDepthStencilView->svga.format           = vboxDXDxgiToSvgaFormat(pDepthStencilView->Format);
    pDepthStencilView->svga.resourceDimension = d3dToSvgaResourceDimension(pDepthStencilView->ResourceDimension);
    switch (pDepthStencilView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDepthStencilView->svga.mipSlice        = pDepthStencilView->DimensionDesc.Tex1D.MipSlice;
            pDepthStencilView->svga.firstArraySlice = pDepthStencilView->DimensionDesc.Tex1D.FirstArraySlice;
            pDepthStencilView->svga.arraySize       = pDepthStencilView->DimensionDesc.Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDepthStencilView->svga.mipSlice        = pDepthStencilView->DimensionDesc.Tex2D.MipSlice;
            pDepthStencilView->svga.firstArraySlice = pDepthStencilView->DimensionDesc.Tex2D.FirstArraySlice;
            pDepthStencilView->svga.arraySize       = pDepthStencilView->DimensionDesc.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pDepthStencilView->svga.mipSlice        = pDepthStencilView->DimensionDesc.TexCube.MipSlice;
            pDepthStencilView->svga.firstArraySlice = pDepthStencilView->DimensionDesc.TexCube.FirstArraySlice;
            pDepthStencilView->svga.arraySize       = pDepthStencilView->DimensionDesc.TexCube.ArraySize;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            RTHandleTableFree(pDevice->hHTDepthStencilView, pDepthStencilView->uDepthStencilViewId);
            return;
    }
    pDepthStencilView->svga.flags = pDepthStencilView->Flags;

    vgpu10DefineDepthStencilView(pDevice, pDepthStencilView->uDepthStencilViewId, vboxDXGetKMResource(pDepthStencilView->pResource),
                                 pDepthStencilView->svga.format, pDepthStencilView->svga.resourceDimension,
                                 pDepthStencilView->svga.mipSlice, pDepthStencilView->svga.firstArraySlice,
                                 pDepthStencilView->svga.arraySize, pDepthStencilView->svga.flags);

    pDepthStencilView->fDefined = true;
    RTListAppend(&pDepthStencilView->pResource->listRTV, &pDepthStencilView->nodeView);
}


void vboxDXClearDepthStencilView(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView,
                                 UINT Flags, FLOAT Depth, UINT8 Stencil)
{
    uint16_t svgaFlags = 0;
    if (Flags & D3D10_DDI_CLEAR_DEPTH)
        svgaFlags |= SVGA3D_CLEAR_DEPTH;
    if (Flags & D3D10_DDI_CLEAR_STENCIL)
        svgaFlags |= SVGA3D_CLEAR_STENCIL;
    vgpu10ClearDepthStencilView(pDevice, svgaFlags, Stencil, pDepthStencilView->uDepthStencilViewId,
                                vboxDXGetKMResource(pDepthStencilView->pResource), Depth);
}


void vboxDXDestroyDepthStencilView(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView)
{
    if (pDevice->pipeline.pDepthStencilView == pDepthStencilView)
    {
        DEBUG_BREAKPOINT_TEST();
    }

    RTListNodeRemove(&pDepthStencilView->nodeView);

    vgpu10DestroyDepthStencilView(pDevice, pDepthStencilView->uDepthStencilViewId,
                                  vboxDXGetKMResource(pDepthStencilView->pResource));
    RTHandleTableFree(pDevice->hHTDepthStencilView, pDepthStencilView->uDepthStencilViewId);
}


void vboxDXSetRenderTargets(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView,
                            uint32_t NumRTVs, UINT ClearSlots, PVBOXDXRENDERTARGETVIEW *papRenderTargetViews)
{
    /* Update the pipeline state. */
    for (unsigned i = 0; i < NumRTVs; ++i)
        pDevice->pipeline.apRenderTargetViews[i] = papRenderTargetViews[i];
    pDevice->pipeline.cRenderTargetViews = NumRTVs;

    for (unsigned i = 0; i < ClearSlots; ++i)
        pDevice->pipeline.apRenderTargetViews[NumRTVs + i] = NULL;

    /* 'ClearSlots' does not cover all previously bound RTs sometimes.
     * "The range of render target surfaces between the number that NumViews specifies
     *  and the maximum number of render target surfaces that are allowed is required
     *  to contain all NULL or unbound values."
     */
    for (unsigned i = NumRTVs + ClearSlots; i < SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS; ++i)
    {
        if (pDevice->pipeline.apRenderTargetViews[i])
        {
            ClearSlots = i - NumRTVs + 1;
            pDevice->pipeline.apRenderTargetViews[i] = NULL;
        }
    }

    pDevice->pipeline.pDepthStencilView = pDepthStencilView;

    /* Fetch view ids and kernel mode resources. */
    uint32_t aRenderTargetViewIds[SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS];
    PVBOXDXKMRESOURCE aViewKMResources[SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS];
    for (unsigned i = 0; i < NumRTVs; ++i)
    {
        PVBOXDXRENDERTARGETVIEW pRenderTargetView = papRenderTargetViews[i];
        aRenderTargetViewIds[i] = pRenderTargetView ? pRenderTargetView->uRenderTargetViewId : SVGA3D_INVALID_ID;
        aViewKMResources[i] = pRenderTargetView ? vboxDXGetKMResource(pRenderTargetView->pResource) : NULL;
    }

    uint32_t DepthStencilViewId = pDepthStencilView ? pDepthStencilView->uDepthStencilViewId : SVGA3D_INVALID_ID;

    vgpu10SetRenderTargets(pDevice, DepthStencilViewId, pDepthStencilView ? vboxDXGetKMResource(pDepthStencilView->pResource) : NULL,
                           NumRTVs, ClearSlots, aRenderTargetViewIds,
                           aViewKMResources);
}


void vboxDXSetShaderResourceViews(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, uint32_t StartSlot,
                                  uint32_t NumViews, PVBOXDXSHADERRESOURCEVIEW const *papViews)
{
    Assert(NumViews <= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
    NumViews = RT_MIN(NumViews, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

    /* Update the pipeline state. */
    PVBOXDXSRVSTATE pSRVS = &pDevice->pipeline.aSRVs[enmShaderType - SVGA3D_SHADERTYPE_MIN];

    for (unsigned i = 0; i < NumViews; ++i)
        pSRVS->apShaderResourceView[StartSlot + i] = papViews[i];

    uint32_t cSRV = RT_MAX(pSRVS->cShaderResourceView, StartSlot + NumViews);
    while (cSRV)
    {
        if (pSRVS->apShaderResourceView[cSRV - 1])
            break;
        --cSRV;
    }
    pSRVS->cShaderResourceView = cSRV;

    /* Fetch View ids and kernel mode resources. */
    uint32_t aViewIds[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    PVBOXDXKMRESOURCE aViewKMResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    for (unsigned i = 0; i < NumViews; ++i)
    {
        VBOXDXSHADERRESOURCEVIEW *pView = papViews[i];
        aViewIds[i] = pView ? pView->uShaderResourceViewId : SVGA3D_INVALID_ID;
        aViewKMResources[i] = pView ? vboxDXGetKMResource(pView->pResource) : NULL;
    }

    vgpu10SetShaderResources(pDevice, enmShaderType, StartSlot, NumViews, aViewIds, aViewKMResources);
}


void vboxDXSetConstantBuffers(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, UINT StartSlot, UINT NumBuffers,
                              PVBOXDX_RESOURCE *papBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
    AssertReturnVoidStmt(  StartSlot < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
                         && NumBuffers <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
                         && StartSlot + NumBuffers <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    /* Remember which buffers must be set. The buffers will be actual set right before a draw call
     * because the host requires the updates of the buffers content to be done prior to setting the buffers.
     * SetSingleConstantBuffer command creates the actual buffer on the host using the current content,
     * so SetSingleConstantBuffer followed by Update will not update the buffer.
     */
    PVBOXDXCONSTANTBUFFERSSTATE pCBS = &pDevice->pipeline.aConstantBuffers[enmShaderType - SVGA3D_SHADERTYPE_MIN];

    for (unsigned i = 0; i < NumBuffers; ++i)
    {
        PVBOXDX_RESOURCE pResource = papBuffers[i];
        pCBS->apResource[StartSlot + i] = pResource;
        if (pResource)
        {
            uint32_t const cMaxConstants = pResource->pKMResource->AllocationDesc.cbAllocation / (4 * sizeof(UINT));
            uint32_t const FirstConstant = pFirstConstant ? pFirstConstant[i] : 0;
            uint32_t NumConstants = pNumConstants ? pNumConstants[i] : cMaxConstants;
            AssertReturnVoidStmt(FirstConstant < cMaxConstants,
                             pCBS->apResource[StartSlot + i] = NULL;
                             vboxDXDeviceSetError(pDevice, E_INVALIDARG));

            if (NumConstants > cMaxConstants - FirstConstant)
                NumConstants = cMaxConstants - FirstConstant;

            pCBS->aFirstConstant[StartSlot + i] = FirstConstant;
            pCBS->aNumConstants[StartSlot + i] = NumConstants;
        }
        else
        {
            pCBS->aFirstConstant[StartSlot + i] = 0;
            pCBS->aNumConstants[StartSlot + i] = 0;
        }
        LogFunc(("type %d, slot %d, first %d, num %d, cbAllocation %d",
                 enmShaderType, StartSlot + i, pCBS->aFirstConstant[StartSlot + i], pCBS->aNumConstants[StartSlot + i],
                 pResource ? pResource->pKMResource->AllocationDesc.cbAllocation : -1));
    }

    /* Join the current range and the new range. */
    if (pCBS->NumBuffers == 0)
    {
        pCBS->StartSlot = StartSlot;
        pCBS->NumBuffers = NumBuffers;
    }
    else
    {
        UINT FirstSlot = RT_MIN(StartSlot, pCBS->StartSlot);
        UINT EndSlot = RT_MAX(pCBS->StartSlot + pCBS->NumBuffers, StartSlot + NumBuffers);
        pCBS->StartSlot = FirstSlot;
        pCBS->NumBuffers = EndSlot - FirstSlot;
    }
}


void vboxDXResourceCopyRegion(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, UINT DstSubresource,
                              UINT DstX, UINT DstY, UINT DstZ, PVBOXDX_RESOURCE pSrcResource, UINT SrcSubresource,
                              const D3D10_DDI_BOX *pSrcBox, UINT CopyFlags)
{
    RT_NOREF(CopyFlags);

    SVGA3dBox srcBox;
    if (pSrcBox)
    {
        srcBox.x = pSrcBox->left;
        srcBox.y = pSrcBox->top;
        srcBox.z = pSrcBox->front;
        srcBox.w = pSrcBox->right - pSrcBox->left;
        srcBox.h = pSrcBox->bottom - pSrcBox->top;
        srcBox.d = pSrcBox->back - pSrcBox->front;
    }
    else
    {
        vboxDXGetSubresourceBox(vboxDXGetAllocationDesc(pSrcResource), SrcSubresource, &srcBox);
    }

    vgpu10ResourceCopyRegion(pDevice, vboxDXGetKMResource(pDstResource), DstSubresource,
                              DstX, DstY, DstZ, vboxDXGetKMResource(pSrcResource), SrcSubresource, srcBox);
}


void vboxDXResourceCopy(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, PVBOXDX_RESOURCE pSrcResource)
{
    vgpu10ResourceCopy(pDevice, vboxDXGetKMResource(pDstResource), vboxDXGetKMResource(pSrcResource));
}


void vboxDXResourceResolveSubresource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, UINT DstSubresource,
                                      PVBOXDX_RESOURCE pSrcResource, UINT SrcSubresource, DXGI_FORMAT ResolveFormat)
{
    SVGA3dSurfaceFormat const copyFormat = vboxDXDxgiToSvgaFormat(ResolveFormat);
    vgpu10ResolveCopy(pDevice, vboxDXGetKMResource(pDstResource), DstSubresource,
                      vboxDXGetKMResource(pSrcResource), SrcSubresource, copyFormat);
}

static void vboxDXUndefineResourceViews(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource)
{
    VBOXDXSHADERRESOURCEVIEW *pShaderResourceView;
    RTListForEach(&pResource->listSRV, pShaderResourceView, VBOXDXSHADERRESOURCEVIEW, nodeView)
    {
        if (pShaderResourceView->fDefined)
        {
            vgpu10DestroyShaderResourceView(pDevice, pShaderResourceView->uShaderResourceViewId,
                                            vboxDXGetKMResource(pShaderResourceView->pResource));
            pShaderResourceView->fDefined = false;
        }
    }

    VBOXDXRENDERTARGETVIEW *pRenderTargetView;
    RTListForEach(&pResource->listRTV, pRenderTargetView, VBOXDXRENDERTARGETVIEW, nodeView)
    {
        if (pRenderTargetView->fDefined)
        {
            vgpu10DestroyRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId,
                                          vboxDXGetKMResource(pRenderTargetView->pResource));
            pRenderTargetView->fDefined = false;
        }
    }

    VBOXDXDEPTHSTENCILVIEW *pDepthStencilView;
    RTListForEach(&pResource->listDSV, pDepthStencilView, VBOXDXDEPTHSTENCILVIEW, nodeView)
    {
        if (pDepthStencilView->fDefined)
        {
            vgpu10DestroyDepthStencilView(pDevice, pDepthStencilView->uDepthStencilViewId,
                                          vboxDXGetKMResource(pDepthStencilView->pResource));
            pDepthStencilView->fDefined = false;
        }
    }

    VBOXDXUNORDEREDACCESSVIEW *pUnorderedAccessView;
    RTListForEach(&pResource->listUAV, pUnorderedAccessView, VBOXDXUNORDEREDACCESSVIEW, nodeView)
    {
        if (pUnorderedAccessView->fDefined)
        {
            vgpu10DestroyUAView(pDevice, pUnorderedAccessView->uUnorderedAccessViewId,
                                vboxDXGetKMResource(pUnorderedAccessView->pResource));
            pUnorderedAccessView->fDefined = false;
        }
    }

    VBOXDXVIDEODECODEROUTPUTVIEW *pVDOV;
    RTListForEach(&pResource->listVDOV, pVDOV, VBOXDXVIDEODECODEROUTPUTVIEW, nodeView)
    {
        if (pVDOV->fDefined)
        {
            vgpu10DestroyVideoDecoderOutputView(pDevice, pVDOV->uVideoDecoderOutputViewId,
                                                vboxDXGetKMResource(pVDOV->pResource));
            pVDOV->fDefined = false;
        }
    }

    VBOXDXVIDEOPROCESSORINPUTVIEW *pVPIV;
    RTListForEach(&pResource->listVPIV, pVPIV, VBOXDXVIDEOPROCESSORINPUTVIEW, nodeView)
    {
        if (pVPIV->fDefined)
        {
            vgpu10DestroyVideoProcessorInputView(pDevice, pVPIV->uVideoProcessorInputViewId,
                                                 vboxDXGetKMResource(pVPIV->pResource));
            pVPIV->fDefined = false;
        }
    }


    VBOXDXVIDEOPROCESSOROUTPUTVIEW *pVPOV;
    RTListForEach(&pResource->listVPOV, pVPOV, VBOXDXVIDEOPROCESSOROUTPUTVIEW, nodeView)
    {
        if (pVPOV->fDefined)
        {
            vgpu10DestroyVideoProcessorOutputView(pDevice, pVPOV->uVideoProcessorOutputViewId,
                                                  vboxDXGetKMResource(pVPOV->pResource));
            pVPOV->fDefined = false;
        }
    }
}


static void vboxDXRedefineResourceViews(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource)
{
    VBOXDXSHADERRESOURCEVIEW *pShaderResourceView;
    RTListForEach(&pResource->listSRV, pShaderResourceView, VBOXDXSHADERRESOURCEVIEW, nodeView)
    {
        if (!pShaderResourceView->fDefined)
        {
            vgpu10DefineShaderResourceView(pDevice, pShaderResourceView->uShaderResourceViewId, vboxDXGetKMResource(pShaderResourceView->pResource),
                                           pShaderResourceView->svga.format, pShaderResourceView->svga.resourceDimension,
                                           &pShaderResourceView->svga.desc);
            pShaderResourceView->fDefined = true;
        }
    }

    VBOXDXRENDERTARGETVIEW *pRenderTargetView;
    RTListForEach(&pResource->listRTV, pRenderTargetView, VBOXDXRENDERTARGETVIEW, nodeView)
    {
        if (!pRenderTargetView->fDefined)
        {
            vgpu10DefineRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId, vboxDXGetKMResource(pRenderTargetView->pResource),
                                         pRenderTargetView->svga.format, pRenderTargetView->svga.resourceDimension,
                                         &pRenderTargetView->svga.desc);
            pRenderTargetView->fDefined = true;
        }
    }

    VBOXDXDEPTHSTENCILVIEW *pDepthStencilView;
    RTListForEach(&pResource->listDSV, pDepthStencilView, VBOXDXDEPTHSTENCILVIEW, nodeView)
    {
        if (!pDepthStencilView->fDefined)
        {
            vgpu10DefineDepthStencilView(pDevice, pDepthStencilView->uDepthStencilViewId, vboxDXGetKMResource(pDepthStencilView->pResource),
                                         pDepthStencilView->svga.format, pDepthStencilView->svga.resourceDimension,
                                         pDepthStencilView->svga.mipSlice, pDepthStencilView->svga.firstArraySlice,
                                         pDepthStencilView->svga.arraySize, pDepthStencilView->svga.flags);
            pDepthStencilView->fDefined = true;
        }
    }

    VBOXDXUNORDEREDACCESSVIEW *pUnorderedAccessView;
    RTListForEach(&pResource->listUAV, pUnorderedAccessView, VBOXDXUNORDEREDACCESSVIEW, nodeView)
    {
        if (!pUnorderedAccessView->fDefined)
        {
            vgpu10DefineUAView(pDevice, pUnorderedAccessView->uUnorderedAccessViewId, vboxDXGetKMResource(pUnorderedAccessView->pResource),
                               pUnorderedAccessView->svga.format, pUnorderedAccessView->svga.resourceDimension,
                               pUnorderedAccessView->svga.desc);
            pUnorderedAccessView->fDefined = true;
        }
    }

    VBOXDXVIDEODECODEROUTPUTVIEW *pVDOV;
    RTListForEach(&pResource->listVDOV, pVDOV, VBOXDXVIDEODECODEROUTPUTVIEW, nodeView)
    {
        if (!pVDOV->fDefined)
        {
            vgpu10DefineVideoDecoderOutputView(pDevice, pVDOV->uVideoDecoderOutputViewId,
                                               vboxDXGetKMResource(pVDOV->pResource),
                                               pVDOV->svga.desc);
            pVDOV->fDefined = true;
        }
    }

    VBOXDXVIDEOPROCESSORINPUTVIEW *pVPIV;
    RTListForEach(&pResource->listVPIV, pVPIV, VBOXDXVIDEOPROCESSORINPUTVIEW, nodeView)
    {
        if (!pVPIV->fDefined)
        {
            vgpu10DefineVideoProcessorInputView(pDevice, pVPIV->uVideoProcessorInputViewId,
                                                vboxDXGetKMResource(pVPIV->pResource),
                                                pVPIV->svga.ContentDesc, pVPIV->svga.VPIVDesc);
            pVPIV->fDefined = true;
        }
    }

    VBOXDXVIDEOPROCESSOROUTPUTVIEW *pVPOV;
    RTListForEach(&pResource->listVPOV, pVPOV, VBOXDXVIDEOPROCESSOROUTPUTVIEW, nodeView)
    {
        if (!pVPOV->fDefined)
        {
            vgpu10DefineVideoProcessorOutputView(pDevice, pVPOV->uVideoProcessorOutputViewId,
                                                 vboxDXGetKMResource(pVPOV->pResource),
                                                 pVPOV->svga.ContentDesc, pVPOV->svga.VPOVDesc);
            pVPOV->fDefined = true;
        }
    }
}


static void vboxdxUnbindResourceViews(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource)
{
    VBOXDXSHADERRESOURCEVIEW *pShaderResourceView;
    RTListForEach(&pResource->listSRV, pShaderResourceView, VBOXDXSHADERRESOURCEVIEW, nodeView)
    {
        /* Search this view in the pipeline state. */
        for (unsigned idxShaderType = 0; idxShaderType < RT_ELEMENTS(pDevice->pipeline.aSRVs); ++idxShaderType)
        {
            SVGA3dShaderType const enmShaderType = (SVGA3dShaderType)(idxShaderType + SVGA3D_SHADERTYPE_MIN);

            PVBOXDXSRVSTATE pSRVS = &pDevice->pipeline.aSRVs[idxShaderType];
            for (unsigned i = 0; i < pSRVS->cShaderResourceView; ++i)
            {
                if (pSRVS->apShaderResourceView[i] == pShaderResourceView)
                {
                    uint32_t id = SVGA3D_INVALID_ID;
                    vgpu10SetShaderResources(pDevice, enmShaderType, i, 1, &id, NULL);
                }
            }
        }
    }
}


HRESULT vboxDXRotateResourceIdentities(PVBOXDX_DEVICE pDevice, UINT cResources, PVBOXDX_RESOURCE *papResources)
{
#ifdef LOG_ENABLED
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        LogFlowFunc(("Resources[%d]: pResource %p, hAllocation 0x%08x\n", i, pResource, vboxDXGetAllocation(pResource)));

        unsigned iV = 0;
        VBOXDXRENDERTARGETVIEW *pRTV;
        RTListForEach(&pResource->listRTV, pRTV, VBOXDXRENDERTARGETVIEW, nodeView)
        {
            LogFlowFunc(("  RTV[%d]: %p\n", iV, pRTV));
            ++iV;
        }

        iV = 0;
        VBOXDXSHADERRESOURCEVIEW *pSRV;
        RTListForEach(&pResource->listSRV, pSRV, VBOXDXSHADERRESOURCEVIEW, nodeView)
        {
            LogFlowFunc(("  SRV[%d]: %p\n", iV, pSRV));
            ++iV;
        }
    }

    LogFlowFunc(("Pipeline: cRTV %u, cSRV VS %u, PS %u, GS %u, HS %u, DS %u, CS %u\n",
                 pDevice->pipeline.cRenderTargetViews,
                 pDevice->pipeline.aSRVs[0].cShaderResourceView,
                 pDevice->pipeline.aSRVs[1].cShaderResourceView,
                 pDevice->pipeline.aSRVs[2].cShaderResourceView,
                 pDevice->pipeline.aSRVs[3].cShaderResourceView,
                 pDevice->pipeline.aSRVs[4].cShaderResourceView,
                 pDevice->pipeline.aSRVs[5].cShaderResourceView));
    for (unsigned i = 0; i < pDevice->pipeline.cRenderTargetViews; ++i)
        LogFlowFunc(("  RTV[%d]: %p\n", i, pDevice->pipeline.apRenderTargetViews[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[0].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV VS[%d]: %p\n", i, pDevice->pipeline.aSRVs[0].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[1].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV PS[%d]: %p\n", i, pDevice->pipeline.aSRVs[1].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[2].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV GS[%d]: %p\n", i, pDevice->pipeline.aSRVs[2].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[3].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV HS[%d]: %p\n", i, pDevice->pipeline.aSRVs[3].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[4].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV DS[%d]: %p\n", i, pDevice->pipeline.aSRVs[4].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[5].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV CS[%d]: %p\n", i, pDevice->pipeline.aSRVs[5].apShaderResourceView[i]));
#endif

    /** @todo Rebind UAVs which are currently bound to pipeline. */

    /* Unbind current render targets, if a resource is bound as a render target. */
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];

        bool fBound = false;
        VBOXDXRENDERTARGETVIEW *pRenderTargetView;
        RTListForEach(&pResource->listRTV, pRenderTargetView, VBOXDXRENDERTARGETVIEW, nodeView)
        {
            for (unsigned iRTV = 0; iRTV < pDevice->pipeline.cRenderTargetViews; ++iRTV)
            {
                if (pDevice->pipeline.apRenderTargetViews[iRTV] == pRenderTargetView)
                {
                    fBound = true;
                    break;
               }
            }
            if (fBound)
                break;
        }

        if (!fBound)
        {
            VBOXDXDEPTHSTENCILVIEW *pDepthStencilView;
            RTListForEach(&pResource->listDSV, pDepthStencilView, VBOXDXDEPTHSTENCILVIEW, nodeView)
            {
                if (pDevice->pipeline.pDepthStencilView == pDepthStencilView)
                {
                    fBound = true;
                    break;
                }
            }
        }

        if (fBound)
        {
            vgpu10SetRenderTargets(pDevice, SVGA3D_INVALID_ID, NULL, 0, SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS, NULL, NULL);
            break;
        }
    }

    /* Inform the host that views of these resources are not valid anymore. */
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        vboxdxUnbindResourceViews(pDevice, pResource);
        vboxDXUndefineResourceViews(pDevice, pResource);
    }

    /* Rotate allocation handles. The function would be that simple if resources would not have views. */
    PVBOXDXKMRESOURCE pKMResource = papResources[0]->pKMResource;
    for (unsigned i = 0; i < cResources - 1; ++i)
        papResources[i]->pKMResource = papResources[i + 1]->pKMResource;
    papResources[cResources - 1]->pKMResource = pKMResource;

    /* Recreate views for the new hAllocations. */
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        vboxDXRedefineResourceViews(pDevice, pResource);
    }

    /* Reapply pipeline state. "Also, the driver might be required to reapply currently bound views." */
    pDevice->pUMCallbacks->pfnStateVsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
    pDevice->pUMCallbacks->pfnStateGsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
    pDevice->pUMCallbacks->pfnStatePsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
    if (pDevice->uDDIVersion >= D3D11_0_DDI_INTERFACE_VERSION)
    {
        pDevice->pUMCallbacks->pfnStateHsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
        pDevice->pUMCallbacks->pfnStateDsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
        pDevice->pUMCallbacks->pfnStateCsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
        UINT cUAV = pDevice->uDDIVersion >= D3D11_1_DDI_INTERFACE_VERSION
                  ? SVGA3D_DX11_1_MAX_UAVIEWS
                  : SVGA3D_MAX_UAVIEWS;
        pDevice->pUMCallbacks->pfnStateCsUavCb(pDevice->hRTCoreLayer, /* Base */ 0, cUAV);
    }

    pDevice->pUMCallbacks->pfnStateOmRenderTargetsCb(pDevice->hRTCoreLayer);

    return S_OK;
}


static void dxProcessPendingOffers(PVBOXDX_DEVICE pDevice)
{
    PVBOXDXKMRESOURCE pKMResource, pNext;
    RTListForEachSafe(&pDevice->listOfferedResources, pKMResource, pNext, VBOXDXKMRESOURCE, resource.nodeOffered)
    {
        Assert(pKMResource->flags.fPendingOffered);
        Assert(!pKMResource->flags.fOffered);

        if (dxIsResourceInUse(pDevice, pKMResource))
            continue;

        RTListNodeRemove(&pKMResource->resource.nodeOffered);
        pKMResource->flags.fPendingOffered = 0;
        pKMResource->flags.fOffered = 1;

        D3DDDICB_OFFERALLOCATIONS ddiOfferAllocations;
        RT_ZERO(ddiOfferAllocations);
        //ddiOfferAllocations.pResources   = NULL; /* If the driver uses HandleList, it must set pResources to NULL. */
        ddiOfferAllocations.HandleList     = &pKMResource->hAllocation;
        ddiOfferAllocations.NumAllocations = 1;
        ddiOfferAllocations.Priority       = pKMResource->resource.OfferPriority;

        HRESULT hr = pDevice->pRTCallbacks->pfnOfferAllocationsCb(pDevice->hRTDevice.handle, &ddiOfferAllocations);
        LogFlowFunc(("pfnOfferAllocationsCb returned %x", hr));
        Assert(SUCCEEDED(hr)); RT_NOREF(hr);
    }
}


HRESULT vboxDXOfferResources(PVBOXDX_DEVICE pDevice, UINT cResources, PVBOXDX_RESOURCE *papResources, D3DDDI_OFFER_PRIORITY Priority)
{
    AssertReturn(cResources <= SVGA3D_MAX_SURFACE_IDS, E_INVALIDARG);

    /* Allocate an array of allocation handles for pfnOfferAllocationsCb. */
    size_t const cbAlloc = cResources * sizeof(D3DKMT_HANDLE);
    D3DKMT_HANDLE *pahAllocations = (D3DKMT_HANDLE *)RTMemTmpAlloc(cbAlloc);
    AssertReturn(pahAllocations, E_OUTOFMEMORY);

    /* Translate resources to allocation handles and immediately offer idle allocations. */
    UINT cOffered = 0;
    for (UINT i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        if (!pResource || !pResource->pKMResource)
            continue;

        PVBOXDXKMRESOURCE pKMResource = pResource->pKMResource;

        Assert(!pKMResource->flags.fPendingOffered);
        Assert(!pKMResource->flags.fOffered);

        if (!dxIsResourceInUse(pDevice, pKMResource))
        {
            pKMResource->flags.fOffered = 1;
            pahAllocations[cOffered++] = vboxDXGetAllocation(pKMResource);
        }
        else
        {
            pKMResource->flags.fPendingOffered = 1;
            pKMResource->resource.OfferPriority = Priority;
            RTListAppend(&pDevice->listOfferedResources, &pKMResource->resource.nodeOffered);
        }
    }

    HRESULT hr = S_OK;
    if (cOffered)
    {
        D3DDDICB_OFFERALLOCATIONS ddiOfferAllocations;
        RT_ZERO(ddiOfferAllocations);
        //ddiOfferAllocations.pResources   = NULL; /* If the driver uses HandleList, it must set pResources to NULL. */
        ddiOfferAllocations.HandleList     = pahAllocations;
        ddiOfferAllocations.NumAllocations = cOffered;
        ddiOfferAllocations.Priority       = Priority;

        hr = pDevice->pRTCallbacks->pfnOfferAllocationsCb(pDevice->hRTDevice.handle, &ddiOfferAllocations);
        LogFlowFunc(("pfnOfferAllocationsCb returned %x", hr));
        Assert(SUCCEEDED(hr));
    }

    RTMemTmpFree(pahAllocations);
    return hr;
}


HRESULT vboxDXReclaimResources(PVBOXDX_DEVICE pDevice, UINT cResources, PVBOXDX_RESOURCE *papResources, BOOL *paDiscarded)
{
    AssertReturn(cResources <= SVGA3D_MAX_SURFACE_IDS, E_INVALIDARG);

    HRESULT hr = S_OK;

    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        if (!pResource || !pResource->pKMResource)
        {
            if (paDiscarded)
                paDiscarded[i] = TRUE;
            continue;
        }

        PVBOXDXKMRESOURCE pKMResource = pResource->pKMResource;

        /* Not offered resources are not discarded. */
        if (pKMResource->flags.fPendingOffered)
        {
            /* The resource was not yet offered. Cancel the offer request. */
            Assert(!pKMResource->flags.fOffered);
            pKMResource->flags.fPendingOffered = 0;
            RTListNodeRemove(&pKMResource->resource.nodeOffered);
            if (paDiscarded)
                paDiscarded[i] = FALSE;
            continue;
        }

        if (!pKMResource->flags.fOffered)
        {
            /* The resoource was never offered. */
            if (paDiscarded)
                paDiscarded[i] = FALSE;
            continue;
        }

        pKMResource->flags.fOffered = 0;
        /* paDiscarded[i] is updated by pfnReclaimAllocationsCb */

        D3DKMT_HANDLE hAllocation = vboxDXGetAllocation(pKMResource);
        D3DDDICB_RECLAIMALLOCATIONS ddiReclaimAllocations;
        RT_ZERO(ddiReclaimAllocations);
        //ddiReclaimAllocations.pResources   = NULL; /* If the driver uses HandleList, it must set pResources to NULL. */
        ddiReclaimAllocations.HandleList     = &hAllocation;
        ddiReclaimAllocations.pDiscarded     = paDiscarded ? &paDiscarded[i] : NULL;
        ddiReclaimAllocations.NumAllocations = 1;

        hr = pDevice->pRTCallbacks->pfnReclaimAllocationsCb(pDevice->hRTDevice.handle, &ddiReclaimAllocations);
        LogFlowFunc(("pfnReclaimAllocationsCb returned %x", hr));
        AssertBreak(SUCCEEDED(hr));
    }

    return hr;
}


void vboxDXCreateUnorderedAccessView(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView)
{
    int rc = RTHandleTableAlloc(pDevice->hHTUnorderedAccessView, pUnorderedAccessView, &pUnorderedAccessView->uUnorderedAccessViewId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    pUnorderedAccessView->svga.format            = vboxDXDxgiToSvgaFormat(pUnorderedAccessView->Format);
    pUnorderedAccessView->svga.resourceDimension = d3dToSvgaResourceDimension(pUnorderedAccessView->ResourceDimension);
    SVGA3dUAViewDesc *pDesc                      = &pUnorderedAccessView->svga.desc;
    RT_ZERO(*pDesc);
    switch (pUnorderedAccessView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pDesc->buffer.firstElement   = pUnorderedAccessView->DimensionDesc.Buffer.FirstElement;
            pDesc->buffer.numElements    = pUnorderedAccessView->DimensionDesc.Buffer.NumElements;
            pDesc->buffer.flags          = pUnorderedAccessView->DimensionDesc.Buffer.Flags;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDesc->tex.mipSlice          = pUnorderedAccessView->DimensionDesc.Tex1D.MipSlice;
            pDesc->tex.firstArraySlice   = pUnorderedAccessView->DimensionDesc.Tex1D.FirstArraySlice;
            pDesc->tex.arraySize         = pUnorderedAccessView->DimensionDesc.Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDesc->tex.mipSlice          = pUnorderedAccessView->DimensionDesc.Tex2D.MipSlice;
            pDesc->tex.firstArraySlice   = pUnorderedAccessView->DimensionDesc.Tex2D.FirstArraySlice;
            pDesc->tex.arraySize         = pUnorderedAccessView->DimensionDesc.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pDesc->tex3D.mipSlice        = pUnorderedAccessView->DimensionDesc.Tex3D.MipSlice;
            pDesc->tex3D.firstW          = pUnorderedAccessView->DimensionDesc.Tex3D.FirstW;
            pDesc->tex3D.wSize           = pUnorderedAccessView->DimensionDesc.Tex3D.WSize;
            break;
        default:
            RTHandleTableFree(pDevice->hHTUnorderedAccessView, pUnorderedAccessView->uUnorderedAccessViewId);
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            return;
    }

    vgpu10DefineUAView(pDevice, pUnorderedAccessView->uUnorderedAccessViewId, vboxDXGetKMResource(pUnorderedAccessView->pResource),
                       pUnorderedAccessView->svga.format, pUnorderedAccessView->svga.resourceDimension,
                       pUnorderedAccessView->svga.desc);

    pUnorderedAccessView->fDefined = true;
    RTListAppend(&pUnorderedAccessView->pResource->listUAV, &pUnorderedAccessView->nodeView);
}


void vboxDXDestroyUnorderedAccessView(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView)
{
    RTListNodeRemove(&pUnorderedAccessView->nodeView);

    vgpu10DestroyUAView(pDevice, pUnorderedAccessView->uUnorderedAccessViewId,
                        vboxDXGetKMResource(pUnorderedAccessView->pResource));
    RTHandleTableFree(pDevice->hHTUnorderedAccessView, pUnorderedAccessView->uUnorderedAccessViewId);
}


void vboxDXClearUnorderedAccessViewUint(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView, const UINT Values[4])
{
    vgpu10ClearUAViewUint(pDevice, pUnorderedAccessView->uUnorderedAccessViewId,
                          vboxDXGetKMResource(pUnorderedAccessView->pResource), Values);
}


void vboxDXClearUnorderedAccessViewFloat(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView, const FLOAT Values[4])
{
    vgpu10ClearUAViewFloat(pDevice, pUnorderedAccessView->uUnorderedAccessViewId,
                           vboxDXGetKMResource(pUnorderedAccessView->pResource), Values);
}


void vboxDXCsSetUnorderedAccessViews(PVBOXDX_DEVICE pDevice, UINT StartSlot, UINT NumViews, const PVBOXDXUNORDEREDACCESSVIEW *papViews, const UINT* pUAVInitialCounts)
{
    AssertReturnVoidStmt(   NumViews <= SVGA3D_DX11_1_MAX_UAVIEWS
                         && StartSlot < SVGA3D_DX11_1_MAX_UAVIEWS
                         && NumViews + StartSlot <= SVGA3D_DX11_1_MAX_UAVIEWS,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    /* Fetch View ids and kernel mode resources. */
    uint32_t aViewIds[SVGA3D_DX11_1_MAX_UAVIEWS];
    PVBOXDXKMRESOURCE aViewKMResources[SVGA3D_DX11_1_MAX_UAVIEWS];
    for (UINT i = 0; i < NumViews; ++i)
    {
        VBOXDXUNORDEREDACCESSVIEW *pView = papViews[i];
        aViewIds[i] = pView ? pView->uUnorderedAccessViewId : SVGA3D_INVALID_ID;
        aViewKMResources[i] = pView ? vboxDXGetKMResource(pView->pResource) : NULL;
    }

    for (UINT i = 0; i < NumViews; ++i)
    {
        if (aViewIds[i] != SVGA3D_INVALID_ID)
            vgpu10SetStructureCount(pDevice, aViewIds[i], aViewKMResources[i], pUAVInitialCounts[i]);
    }

    vgpu10SetCSUAViews(pDevice, StartSlot, NumViews, aViewIds, aViewKMResources);
}


void vboxDXSetUnorderedAccessViews(PVBOXDX_DEVICE pDevice, UINT StartSlot, UINT NumViews, const PVBOXDXUNORDEREDACCESSVIEW *papViews,
                                   const UINT *pUAVInitialCounts)
{
    AssertReturnVoidStmt(   NumViews <= D3D11_1_UAV_SLOT_COUNT
                         && StartSlot <= SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    /* Fetch View ids and kernel mode resources. */
    uint32_t aViewIds[D3D11_1_UAV_SLOT_COUNT];
    PVBOXDXKMRESOURCE aViewKMResources[SVGA3D_DX11_1_MAX_UAVIEWS];
    for (UINT i = 0; i < NumViews; ++i)
    {
        PVBOXDXUNORDEREDACCESSVIEW pView = papViews[i];
        aViewIds[i] = pView ? pView->uUnorderedAccessViewId : SVGA3D_INVALID_ID;
        aViewKMResources[i] = pView ? vboxDXGetKMResource(pView->pResource) : NULL;
    }

    UINT NumViewsToSet;
    if (pDevice->pipeline.cUnorderedAccessViews > NumViews)
    {
        /* Clear previously set views, which are not used anymore. */
        for (unsigned i = NumViews; i < pDevice->pipeline.cUnorderedAccessViews; ++i)
            aViewIds[i] = SVGA3D_INVALID_ID;

        NumViewsToSet = pDevice->pipeline.cUnorderedAccessViews;
    }
    else
        NumViewsToSet = NumViews;

    pDevice->pipeline.cUnorderedAccessViews = NumViews;

    for (UINT i = 0; i < NumViews; ++i)
    {
        if (aViewIds[i] != SVGA3D_INVALID_ID)
            vgpu10SetStructureCount(pDevice, aViewIds[i], aViewKMResources[i], pUAVInitialCounts[i]);
    }

    vgpu10SetUAViews(pDevice, StartSlot, NumViewsToSet, aViewIds, aViewKMResources);
}


void vboxDXDispatch(PVBOXDX_DEVICE pDevice, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10Dispatch(pDevice, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}


void vboxDXDispatchIndirect(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT AlignedByteOffsetForArgs)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DispatchIndirect(pDevice, vboxDXGetKMResource(pResource), AlignedByteOffsetForArgs);
}


void vboxDXCopyStructureCount(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstBuffer, UINT DstAlignedByteOffset, PVBOXDXUNORDEREDACCESSVIEW pSrcView)
{
    vgpu10CopyStructureCount(pDevice, pSrcView->uUnorderedAccessViewId, vboxDXGetKMResource(pSrcView->pResource),
                            vboxDXGetKMResource(pDstBuffer), DstAlignedByteOffset);
}


HRESULT vboxDXBlt(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, UINT DstSubresource,
                  PVBOXDX_RESOURCE pSrcResource, UINT SrcSubresource,
                  UINT DstLeft, UINT DstTop, UINT DstRight, UINT DstBottom,
                  DXGI_DDI_ARG_BLT_FLAGS Flags, DXGI_DDI_MODE_ROTATION Rotate)
{
    AssertReturn(Rotate == DXGI_DDI_MODE_ROTATION_IDENTITY, DXGI_ERROR_INVALID_CALL);
    AssertReturn(Flags.Resolve == 0, DXGI_ERROR_INVALID_CALL); /** @todo Multisampled resources. */

    SVGA3dBox boxSrc;
    vboxDXGetSubresourceBox(vboxDXGetAllocationDesc(pSrcResource), SrcSubresource, &boxSrc); /* Entire subresource. */

    SVGA3dBox boxDest;
    boxDest.x = DstLeft;
    boxDest.y = DstTop;
    boxDest.z = 0;
    boxDest.w = DstRight - DstLeft;
    boxDest.h = DstBottom - DstTop;
    boxDest.d = 1;

    SVGA3dDXPresentBltMode mode = 0;

    vgpu10PresentBlt(pDevice, vboxDXGetKMResource(pSrcResource), SrcSubresource, vboxDXGetKMResource(pDstResource), DstSubresource,
                     boxSrc, boxDest, mode);
    return S_OK;
}


void vboxDXClearView(PVBOXDX_DEVICE pDevice, D3D11DDI_HANDLETYPE ViewType, void *pView, FLOAT const Color[4], D3D10_DDI_RECT const *pRect, UINT NumRects)
{
    SVGAFifo3dCmdId enmCmdId;
    uint32_t ViewId;
    PVBOXDXKMRESOURCE pViewKMResource;

    /* "Possible types are the following.
     * D3D10DDI_HT_RENDERTARGETVIEW
     * D3D11DDI_HT_UNORDEREDACCESSVIEW
     * Any D3D11_1DDI_HT_VIDEOXXX type"
     */

    switch (ViewType)
    {
        case D3D10DDI_HT_RENDERTARGETVIEW:
        {
            PVBOXDXRENDERTARGETVIEW pRenderTargetView = (PVBOXDXRENDERTARGETVIEW)pView;
            enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_RTV;
            ViewId = pRenderTargetView->uRenderTargetViewId;
            pViewKMResource = vboxDXGetKMResource(pRenderTargetView->pResource);
            break;
        }
        case D3D11DDI_HT_UNORDEREDACCESSVIEW:
        {
            PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView = (PVBOXDXUNORDEREDACCESSVIEW)pView;
            enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_UAV;
            ViewId = pUnorderedAccessView->uUnorderedAccessViewId;
            pViewKMResource = vboxDXGetKMResource(pUnorderedAccessView->pResource);
            break;
        }
        case D3D11_1DDI_HT_VIDEODECODEROUTPUTVIEW:
        {
            PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView = (PVBOXDXVIDEODECODEROUTPUTVIEW)pView;
            enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_VDOV;
            ViewId = pVideoDecoderOutputView->uVideoDecoderOutputViewId;
            pViewKMResource = vboxDXGetKMResource(pVideoDecoderOutputView->pResource);
            break;
        }
        case D3D11_1DDI_HT_VIDEOPROCESSORINPUTVIEW:
        {
            PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)pView;
            enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_VPIV;
            ViewId = pVideoProcessorInputView->uVideoProcessorInputViewId;
            pViewKMResource = vboxDXGetKMResource(pVideoProcessorInputView->pResource);
            break;
        }
        case D3D11_1DDI_HT_VIDEOPROCESSOROUTPUTVIEW:
        {
            PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView = (PVBOXDXVIDEOPROCESSOROUTPUTVIEW)pView;
            enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_VPOV;
            ViewId = pVideoProcessorOutputView->uVideoProcessorOutputViewId;
            pViewKMResource = vboxDXGetKMResource(pVideoProcessorOutputView->pResource);
            break;
        }
        default:
            AssertFailedReturnVoid();
    }

    vgpu10ClearView(pDevice, enmCmdId, ViewId, pViewKMResource, Color, pRect, NumRects);
}


static void dxDeallocateStagingResources(PVBOXDX_DEVICE pDevice)
{
    /* Move staging resources to the deferred destruction queue. */
    PVBOXDXKMRESOURCE pKMResource, pNextKMResource;
    RTListForEachSafe(&pDevice->listStagingResources, pKMResource, pNextKMResource, VBOXDXKMRESOURCE, resource.nodeStaging)
    {
        RTListNodeRemove(&pKMResource->resource.nodeStaging);

        PVBOXDX_RESOURCE pStagingResource = pKMResource->resource.pResource;
        pKMResource->resource.pResource = NULL;

        Assert(pStagingResource->pKMResource == pKMResource);

        /* Remove from the list of active resources. */
        RTListNodeRemove(&pKMResource->nodeResource);
        RTListAppend(&pDevice->listDestroyedResources, &pKMResource->nodeResource);

        /* Staging resources are allocated by the driver. */
        RTMemFree(pStagingResource);
    }
}


static void dxDestroyDeferredResources(PVBOXDX_DEVICE pDevice)
{
    PVBOXDXKMRESOURCE pKMResource, pNext;
    RTListForEachSafe(&pDevice->listDestroyedResources, pKMResource, pNext, VBOXDXKMRESOURCE, nodeResource)
    {
        RTListNodeRemove(&pKMResource->nodeResource);
        vboxDXDeallocateKMResource(pDevice, pKMResource);
    }
}


HRESULT vboxDXFlush(PVBOXDX_DEVICE pDevice, bool fForce)
{
    if (   pDevice->cbCommandBuffer != 0
        || fForce)
    {
        HRESULT hr = vboxDXDeviceFlushCommands(pDevice);
        AssertReturnStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr), hr);
    }

    /* Free the staging resources which used for uploads in this command buffer.
     * They are moved to the deferred destruction queue.
     */
    dxDeallocateStagingResources(pDevice);

    /* Process deferred-destruction queue. */
    dxDestroyDeferredResources(pDevice);

    /* Offer resources which were offered when they were idle. */
    dxProcessPendingOffers(pDevice);

    return S_OK;
}


/*
 *
 * D3D device initialization/termination.
 *
 */

static HRESULT vboxDXCreateKernelContextForDevice(PVBOXDX_DEVICE pDevice)
{
    HRESULT hr;

    VBOXWDDM_CREATECONTEXT_INFO privateData;
    RT_ZERO(privateData);
    privateData.enmType = VBOXWDDM_CONTEXT_TYPE_VMSVGA_D3D;
    privateData.u32IfVersion = 11; /** @todo This is not really used by miniport. */
    privateData.u.vmsvga.u32Flags = VBOXWDDM_F_GA_CONTEXT_VGPU10;

    D3DDDICB_CREATECONTEXT ddiCreateContext;
    RT_ZERO(ddiCreateContext);
    ddiCreateContext.pPrivateDriverData = &privateData;
    ddiCreateContext.PrivateDriverDataSize = sizeof(privateData);

    hr = pDevice->pRTCallbacks->pfnCreateContextCb(pDevice->hRTDevice.handle, &ddiCreateContext);
    LogFlowFunc(("hr %d, hContext 0x%p, CommandBufferSize 0x%x, AllocationListSize 0x%x, PatchLocationListSize 0x%x",
        hr, ddiCreateContext.hContext, ddiCreateContext.CommandBufferSize,
        ddiCreateContext.AllocationListSize, ddiCreateContext.PatchLocationListSize));
    if (SUCCEEDED(hr))
    {
        pDevice->hContext              = ddiCreateContext.hContext;
        pDevice->pCommandBuffer        = ddiCreateContext.pCommandBuffer;
        pDevice->CommandBufferSize     = ddiCreateContext.CommandBufferSize - sizeof(VBOXDXCMDBUFSUFFIX);
        pDevice->pAllocationList       = ddiCreateContext.pAllocationList;
        pDevice->AllocationListSize    = ddiCreateContext.AllocationListSize - VBOXDX_CMD_BUF_SUFFIX_ALLOCATION_COUNT;
        pDevice->pPatchLocationList    = ddiCreateContext.pPatchLocationList;
        pDevice->PatchLocationListSize = ddiCreateContext.PatchLocationListSize - VBOXDX_CMD_BUF_SUFFIX_PATCH_COUNT;

        pDevice->cbCommandBuffer   = 0;
        pDevice->cbCommandReserved = 0;

        pDevice->RenderCbSequence = 0;
    }
    return hr;
}


static int dxTransientHeapInit(PVBOXDX_DEVICE pDevice, VBOXDXTRANSIENTHEAP *pHeap)
{
    pHeap->pDevice = pDevice;

    pHeap->pTransientHeapBuffer = vboxDXCreateStagingBuffer(pDevice, VBOXDX_TRANSIENT_HEAP_SIZE);
    AssertReturn(pHeap->pTransientHeapBuffer, VERR_NO_MEMORY);

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = vboxDXGetAllocation(pHeap->pTransientHeapBuffer);
    ddiLock.Flags.WriteOnly = 1;
    ddiLock.Flags.IgnoreSync = 1;
    ddiLock.Flags.DonotWait = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);
    pHeap->pvTransientHeapMapped = ddiLock.pData;

    RTListInit(&pHeap->listBlocks);
    RTListInit(&pHeap->listFreeBlocks);
    pHeap->treeFreeBlocks = NULL;

    /* The first free block for entire buffer. */
    VBOXDXTRANSIENTHEAPBLOCKDESC *pFreeBlock = dxTransientBlockAllocate(pHeap, 0, VBOXDX_TRANSIENT_HEAP_SIZE);
    AssertReturn(pFreeBlock, VERR_NO_MEMORY);
    RTListAppend(&pHeap->listBlocks, &pFreeBlock->nodeTransientHeap);
    RTListAppend(&pHeap->listFreeBlocks, &pFreeBlock->nodeTransientBlock);

    pHeap->pCurrentBatch = dxTransientHeapBatchAllocate(pHeap);
    AssertReturn(pHeap->pCurrentBatch, VERR_NO_MEMORY);

    RTListInit(&pHeap->listTransientBatches);
    RTListInit(&pHeap->listLookasideBatches);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) dxDestroyFreeBlocksNodeCb(PAVLU32NODECORE pNode, void *pvUser)
{
    VBOXDXTRANSIENTHEAPFREEBLOCKS *p = (VBOXDXTRANSIENTHEAPFREEBLOCKS *)pNode;
    VBOXDXTRANSIENTHEAP *pHeap = (VBOXDXTRANSIENTHEAP *)pvUser;

    RT_NOREF(pHeap);
    RTMemFree(p);
    return 0;
}


static void dxTransientHeapDestroy(VBOXDXTRANSIENTHEAP *pHeap)
{
    if (pHeap->pCurrentBatch)
    {
        Assert(RTListIsEmpty(&pHeap->pCurrentBatch->listPendingBlocks)); /* Flush submitted all pending blocks. */
        dxTransientHeapBatchDeallocate(pHeap->pCurrentBatch);
        pHeap->pCurrentBatch = NULL;
    }

    if (!RTListIsEmpty(&pHeap->listTransientBatches))
    {
        /// @todo This should not happen. Wait for the last query.
        DEBUG_BREAKPOINT_TEST();
    }

    VBOXDXTRANSIENTHEAPBATCH *pBatch, *pNext;
    RTListForEachSafe(&pHeap->listLookasideBatches, pBatch, pNext, VBOXDXTRANSIENTHEAPBATCH, nodeTransientBatch)
    {
        RTListNodeRemove(&pBatch->nodeTransientBatch);
        dxTransientHeapBatchDeallocate(pBatch);
    }

    VBOXDXTRANSIENTHEAPBLOCKDESC *pBlock, *pBlockNext;
    RTListForEachSafe(&pHeap->listBlocks, pBlock, pBlockNext, VBOXDXTRANSIENTHEAPBLOCKDESC, nodeTransientHeap)
    {
        RTListNodeRemove(&pBlock->nodeTransientBlock);
        RTListNodeRemove(&pBlock->nodeTransientHeap);
        dxTransientBlockDeallocate(pBlock);
    }

    RTAvlU32Destroy(&pHeap->treeFreeBlocks, dxDestroyFreeBlocksNodeCb, pHeap);

    if (pHeap->pvTransientHeapMapped)
    {
        D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pHeap->pTransientHeapBuffer);

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &hAllocation;
        HRESULT hr = pHeap->pDevice->pRTCallbacks->pfnUnlockCb(pHeap->pDevice->hRTDevice.handle, &ddiUnlock);
        Assert(SUCCEEDED(hr)); RT_NOREF(hr);
        pHeap->pvTransientHeapMapped = NULL;
    }

    if (pHeap->pTransientHeapBuffer)
    {
        vboxDXDestroyResource(pHeap->pDevice, pHeap->pTransientHeapBuffer);
        pHeap->pTransientHeapBuffer = NULL;
    }
}


static int vboxDXDeviceCreateObjects(PVBOXDX_DEVICE pDevice)
{
    int rc;

    rc = RTHandleTableCreateEx(&pDevice->hHTBlendState, /* fFlags */ 0, /* uBase */ 0,
                               D3D10_REQ_BLEND_OBJECT_COUNT_PER_CONTEXT, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTDepthStencilState, /* fFlags */ 0, /* uBase */ 0,
                               D3D10_REQ_DEPTH_STENCIL_OBJECT_COUNT_PER_CONTEXT, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTRasterizerState, /* fFlags */ 0, /* uBase */ 0,
                               D3D10_REQ_RASTERIZER_OBJECT_COUNT_PER_CONTEXT, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTSamplerState, /* fFlags */ 0, /* uBase */ 0,
                               D3D10_REQ_SAMPLER_OBJECT_COUNT_PER_CONTEXT, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTElementLayout, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTShader, /* fFlags */ 0, /* uBase */ 0,
                               SVGA3D_MAX_SHADERIDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTShaderResourceView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTRenderTargetView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTDepthStencilView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTQuery, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTUnorderedAccessView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTStreamOutput, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoProcessor, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoDecoderOutputView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoDecoder, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoProcessorInputView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoProcessorOutputView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    RTListInit(&pDevice->listResources);
    RTListInit(&pDevice->listDestroyedResources);
    RTListInit(&pDevice->listStagingResources);
    RTListInit(&pDevice->listOfferedResources);
    RTListInit(&pDevice->listShaders);
    RTListInit(&pDevice->listShadersAllocations);
    RTListInit(&pDevice->listQueries);
    RTListInit(&pDevice->listCOAQuery);
    RTListInit(&pDevice->listCOAStreamOutput);

    pDevice->u64MobFenceValue = 0;

    /* The upload buffer. */
    pDevice->upload.pUploadBuffer = vboxDXCreateStagingBuffer(pDevice, VBOXDX_UPLOAD_BUFFER_SIZE);
    AssertReturn(pDevice->upload.pUploadBuffer, VERR_NO_MEMORY);

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = vboxDXGetAllocation(pDevice->upload.pUploadBuffer);
    ddiLock.Flags.WriteOnly = 1;
    ddiLock.Flags.IgnoreSync = 1;
    ddiLock.Flags.DonotWait = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);
    pDevice->upload.pvUploadBufferMapped = ddiLock.pData;

    pDevice->upload.offFree = 0;
    pDevice->upload.cbFree = VBOXDX_UPLOAD_BUFFER_SIZE;

    pDevice->upload.pCurrentBatch = dxUploadBatchAllocate(pDevice);

    RTListInit(&pDevice->upload.listUploadBatches);
    RTListInit(&pDevice->upload.listLookasideBatches);

    rc = dxTransientHeapInit(pDevice, &pDevice->transientHeap);
    AssertRCReturn(rc, rc);

    /* Fence for context monitoring. */
    hr = vboxdxCreateQueryInternal(pDevice, &pDevice->ContextMonitoring.queryContextMonitoring, D3D10DDI_QUERY_EVENT, 0);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);
    pDevice->ContextMonitoring.pLastCompletedFenceValue =
        (uint64_t *)(  pDevice->ContextMonitoring.queryContextMonitoring.pCOAllocation->co.pu8COMapped
                     + pDevice->ContextMonitoring.queryContextMonitoring.offQuery);
    *pDevice->ContextMonitoring.pLastCompletedFenceValue = 0;
    pDevice->ContextMonitoring.CurrentFenceValue = 1;

    return VINF_SUCCESS;
}


static void vboxDXDeviceDeleteObjects(PVBOXDX_DEVICE pDevice)
{
    if (pDevice->hHTBlendState)
    {
        RTHandleTableDestroy(pDevice->hHTBlendState, NULL, NULL);
        pDevice->hHTBlendState = 0;
    }

    if (pDevice->hHTDepthStencilState)
    {
        RTHandleTableDestroy(pDevice->hHTDepthStencilState, NULL, NULL);
        pDevice->hHTDepthStencilState = 0;
    }

    if (pDevice->hHTRasterizerState)
    {
        RTHandleTableDestroy(pDevice->hHTRasterizerState, NULL, NULL);
        pDevice->hHTRasterizerState = 0;
    }

    if (pDevice->hHTSamplerState)
    {
        RTHandleTableDestroy(pDevice->hHTSamplerState, NULL, NULL);
        pDevice->hHTSamplerState = 0;
    }

    if (pDevice->hHTElementLayout)
    {
        RTHandleTableDestroy(pDevice->hHTElementLayout, NULL, NULL);
        pDevice->hHTElementLayout = 0;
    }

    if (pDevice->hHTShader)
    {
        RTHandleTableDestroy(pDevice->hHTShader, NULL, NULL);
        pDevice->hHTShader = 0;
    }

    if (pDevice->hHTShaderResourceView)
    {
        RTHandleTableDestroy(pDevice->hHTShaderResourceView, NULL, NULL);
        pDevice->hHTShaderResourceView = 0;
    }

    if (pDevice->hHTRenderTargetView)
    {
        RTHandleTableDestroy(pDevice->hHTRenderTargetView, NULL, NULL);
        pDevice->hHTRenderTargetView = 0;
    }

    if (pDevice->hHTDepthStencilView)
    {
        RTHandleTableDestroy(pDevice->hHTDepthStencilView, NULL, NULL);
        pDevice->hHTDepthStencilView = 0;
    }

    if (pDevice->hHTQuery)
    {
        RTHandleTableDestroy(pDevice->hHTQuery, NULL, NULL);
        pDevice->hHTQuery = 0;
    }

    if (pDevice->hHTUnorderedAccessView)
    {
        RTHandleTableDestroy(pDevice->hHTUnorderedAccessView, NULL, NULL);
        pDevice->hHTUnorderedAccessView = 0;
    }

    if (pDevice->hHTStreamOutput)
    {
        RTHandleTableDestroy(pDevice->hHTStreamOutput, NULL, NULL);
        pDevice->hHTStreamOutput = 0;
    }

    if (pDevice->hHTVideoProcessor)
    {
        RTHandleTableDestroy(pDevice->hHTVideoProcessor, NULL, NULL);
        pDevice->hHTVideoProcessor = 0;
    }

    if (pDevice->hHTVideoDecoderOutputView)
    {
        RTHandleTableDestroy(pDevice->hHTVideoDecoderOutputView, NULL, NULL);
        pDevice->hHTVideoDecoderOutputView = 0;
    }

    if (pDevice->hHTVideoDecoder)
    {
        RTHandleTableDestroy(pDevice->hHTVideoDecoder, NULL, NULL);
        pDevice->hHTVideoDecoder = 0;
    }

    if (pDevice->hHTVideoProcessorInputView)
    {
        RTHandleTableDestroy(pDevice->hHTVideoProcessorInputView, NULL, NULL);
        pDevice->hHTVideoProcessorInputView = 0;
    }

    if (pDevice->hHTVideoProcessorOutputView)
    {
        RTHandleTableDestroy(pDevice->hHTVideoProcessorOutputView, NULL, NULL);
        pDevice->hHTVideoProcessorOutputView = 0;
    }
}


HRESULT vboxDXDeviceInit(PVBOXDX_DEVICE pDevice)
{
    HRESULT hr = vboxDXCreateKernelContextForDevice(pDevice);
    AssertReturn(SUCCEEDED(hr), hr);

    int rc = vboxDXDeviceCreateObjects(pDevice);
    if (RT_FAILURE(rc))
        vboxDXDeviceDeleteObjects(pDevice);

    return hr;
}


void vboxDXDestroyDevice(PVBOXDX_DEVICE pDevice)
{
    HRESULT hr;

    /* Flush will deallocate staging resources. */
    vboxDXFlush(pDevice, true);

    dxTransientHeapDestroy(&pDevice->transientHeap);

    if (pDevice->upload.pvUploadBufferMapped)
    {
        D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pDevice->upload.pUploadBuffer);

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &hAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        Assert(SUCCEEDED(hr)); RT_NOREF(hr);
        pDevice->upload.pvUploadBufferMapped = NULL;
    }

    /* pDevice->upload.pUploadBuffer will be deleted as part of pDevice->listResources cleanup below. */

    if (pDevice->upload.pCurrentBatch)
    {
        Assert(pDevice->upload.pCurrentBatch->cbBatch == 0); /* Flush submitted all pending uploads. */
        dxUploadBatchDeallocate(pDevice, pDevice->upload.pCurrentBatch);
        pDevice->upload.pCurrentBatch = NULL;
    }

    if (!RTListIsEmpty(&pDevice->upload.listUploadBatches))
    {
        /// @todo This should not happen. Wait for the last query.
        DEBUG_BREAKPOINT_TEST();
    }

    VBOXDXUPLOADBATCH *pBatch, *pNext;
    RTListForEachSafe(&pDevice->upload.listLookasideBatches, pBatch, pNext, VBOXDXUPLOADBATCH, nodeUploadBatch)
    {
        dxUploadBatchDeallocate(pDevice, pBatch);
    }

    PVBOXDXKMRESOURCE pKMResource, pNextKMResource;
    RTListForEachSafe(&pDevice->listResources, pKMResource, pNextKMResource, VBOXDXKMRESOURCE, nodeResource)
    {
        if (pKMResource->AllocationDesc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
            vboxDXDestroyResource(pDevice, pKMResource->resource.pResource);
    }

    dxDestroyDeferredResources(pDevice);

    if (!RTListIsEmpty(&pDevice->listOfferedResources))
    {
        /// @todo This should not happen. Wait for the last query.
        DEBUG_BREAKPOINT_TEST();
    }

    if (pDevice->ContextMonitoring.queryContextMonitoring.pCOAllocation)
        vboxDXDestroyQuery(pDevice, &pDevice->ContextMonitoring.queryContextMonitoring);

    PVBOXDXSHADER pShader, pNextShader;
    RTListForEachSafe(&pDevice->listShaders, pShader, pNextShader, VBOXDXSHADER, node)
        vboxDXDestroyShader(pDevice, pShader);

    PVBOXDXQUERY pQuery, pNextQuery;
    RTListForEachSafe(&pDevice->listQueries, pQuery, pNextQuery, VBOXDXQUERY, nodeQuery)
        vboxDXDestroyQuery(pDevice, pQuery);

    PVBOXDXKMRESOURCE pCOA, pNextCOA;
    RTListForEachSafe(&pDevice->listCOAQuery, pCOA, pNextCOA, VBOXDXKMRESOURCE, co.nodeAllocationsChain)
        vboxDXDestroyCOAllocation(pDevice, pCOA);
    RTListForEachSafe(&pDevice->listCOAStreamOutput, pCOA, pNextCOA, VBOXDXKMRESOURCE, co.nodeAllocationsChain)
        vboxDXDestroyCOAllocation(pDevice, pCOA);

    RTListForEachSafe(&pDevice->listResources, pKMResource, pNextKMResource, VBOXDXKMRESOURCE, nodeResource)
    {
        RTListNodeRemove(&pKMResource->nodeResource);
        vboxDXDeallocateKMResource(pDevice, pKMResource);
    }

    D3DDDICB_DESTROYCONTEXT ddiDestroyContext;
    ddiDestroyContext.hContext = pDevice->hContext;

    hr = pDevice->pRTCallbacks->pfnDestroyContextCb(pDevice->hRTDevice.handle, &ddiDestroyContext);
    LogFlowFunc(("hr %d, hContext 0x%p",  hr, pDevice->hContext)); RT_NOREF(hr);

    vboxDXDeviceDeleteObjects(pDevice);

    RTMemFree(pDevice->VideoDevice.paDecodeProfile);
    pDevice->VideoDevice.paDecodeProfile = 0;

    RTMemFree(pDevice->VideoDevice.config.pConfigInfo);
    pDevice->VideoDevice.config.pConfigInfo = 0;
}
