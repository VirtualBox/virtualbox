/* $Id: VCICache.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VCICacheCore - VirtualBox Cache Image, Core Code.
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
#define LOG_GROUP LOG_GROUP_VD_RAW /** @todo logging group */
#include <VBox/vd-cache-backend.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/file.h>

#include "VDBackends.h"

/*******************************************************************************
* On disk data structures                                                      *
*******************************************************************************/

/** @note All structures which are written to the disk are written in camel case
 * and packed. */

/** Block size used internally, because we cache sectors the smallest unit we
 * have to care about is 512 bytes. */
#define VCI_BLOCK_SIZE             512

/** Convert block number/size to byte offset/size. */
#define VCI_BLOCK2BYTE(u)          ((uint64_t)(u) << 9)

/** Convert byte offset/size to block number/size. */
#define VCI_BYTE2BLOCK(u)          ((u) >> 9)

/**
 * The VCI header - at the beginning of the file.
 *
 * All entries a stored in little endian order.
 */
#pragma pack(1)
typedef struct VciHdr
{
    /** The signature to identify a cache image. */
    uint32_t    u32Signature;
    /** Version of the layout of metadata in the cache. */
    uint32_t    u32Version;
    /** Maximum size of the cache file in blocks.
     *  This includes all metadata. */
    uint64_t    cBlocksCache;
    /** Flag indicating whether the cache was closed cleanly. */
    uint8_t     fUncleanShutdown;
    /** Cache type. */
    uint32_t    u32CacheType;
    /** Offset of the B+-Tree root in the image in blocks. */
    uint64_t    offTreeRoot;
    /** Offset of the block allocation bitmap in blocks. */
    uint64_t    offBlkMap;
    /** Size of the block allocation bitmap in blocks. */
    uint32_t    cBlkMap;
    /** UUID of the image. */
    RTUUID      uuidImage;
    /** Modification UUID for the cache. */
    RTUUID      uuidModification;
    /** Reserved for future use. */
    uint8_t     abReserved[951];
} VciHdr, *PVciHdr;
#pragma pack()
AssertCompileSize(VciHdr, 2 * VCI_BLOCK_SIZE);

/** VCI signature to identify a valid image. */
#define VCI_HDR_SIGNATURE          UINT32_C(0x00494356) /* \0ICV */
/** Current version we support. */
#define VCI_HDR_VERSION            UINT32_C(0x00000001)

/** Value for an unclean cache shutdown. */
#define VCI_HDR_UNCLEAN_SHUTDOWN   UINT8_C(0x01)
/** Value for a clean cache shutdown. */
#define VCI_HDR_CLEAN_SHUTDOWN     UINT8_C(0x00)

/** Cache type: Dynamic image growing to the maximum value. */
#define VCI_HDR_CACHE_TYPE_DYNAMIC UINT32_C(0x00000001)
/** Cache type: Fixed image, space is preallocated. */
#define VCI_HDR_CACHE_TYPE_FIXED   UINT32_C(0x00000002)

/**
 * On disk representation of an extent describing a range of cached data.
 *
 * All entries a stored in little endian order.
 */
#pragma pack(1)
typedef struct VciCacheExtent
{
    /** Block address of the previous extent in the LRU list. */
    uint64_t    u64ExtentPrev;
    /** Block address of the next extent in the LRU list. */
    uint64_t    u64ExtentNext;
    /** Flags (for compression, encryption etc.) - currently unused and should be always 0. */
    uint8_t     u8Flags;
    /** Reserved */
    uint8_t     u8Reserved;
    /** First block of cached data the extent represents. */
    uint64_t    u64BlockOffset;
    /** Number of blocks the extent represents. */
    uint32_t    u32Blocks;
    /** First block in the image where the data is stored. */
    uint64_t    u64BlockAddr;
} VciCacheExtent, *PVciCacheExtent;
#pragma pack()
AssertCompileSize(VciCacheExtent, 38);

/**
 * On disk representation of an internal node.
 *
 * All entries a stored in little endian order.
 */
#pragma pack(1)
typedef struct VciTreeNodeInternal
{
    /** First block of cached data the internal node represents. */
    uint64_t    u64BlockOffset;
    /** Number of blocks the internal node represents. */
    uint32_t    u32Blocks;
    /** Block address in the image where the next node in the tree is stored. */
    uint64_t    u64ChildAddr;
} VciTreeNodeInternal, *PVciTreeNodeInternal;
#pragma pack()
AssertCompileSize(VciTreeNodeInternal, 20);

/**
 * On-disk representation of a node in the B+-Tree.
 *
 * All entries a stored in little endian order.
 */
#pragma pack(1)
typedef struct VciTreeNode
{
    /** Type of the node (root, internal, leaf). */
    uint8_t     u8Type;
    /** Data in the node. */
    uint8_t     au8Data[4095];
} VciTreeNode, *PVciTreeNode;
#pragma pack()
AssertCompileSize(VciTreeNode, 8 * VCI_BLOCK_SIZE);

/** Node type: Internal node containing links to other nodes (VciTreeNodeInternal). */
#define VCI_TREE_NODE_TYPE_INTERNAL UINT8_C(0x01)
/** Node type: Leaf of the tree (VciCacheExtent). */
#define VCI_TREE_NODE_TYPE_LEAF     UINT8_C(0x02)

/** Number of cache extents described by one node. */
#define VCI_TREE_EXTENTS_PER_NODE        ((sizeof(VciTreeNode)-1) / sizeof(VciCacheExtent))
/** Number of internal nodes managed by one tree node. */
#define VCI_TREE_INTERNAL_NODES_PER_NODE ((sizeof(VciTreeNode)-1) / sizeof(VciTreeNodeInternal))

/**
 * VCI block bitmap header.
 *
 * All entries a stored in little endian order.
 */
#pragma pack(1)
typedef struct VciBlkMap
{
    /** Magic of the block bitmap. */
    uint32_t     u32Magic;
    /** Version of the block bitmap. */
    uint32_t     u32Version;
    /** Number of blocks this block map manages. */
    uint64_t     cBlocks;
    /** Number of free blocks. */
    uint64_t     cBlocksFree;
    /** Number of blocks allocated for metadata. */
    uint64_t     cBlocksAllocMeta;
    /** Number of blocks allocated for actual cached data. */
    uint64_t     cBlocksAllocData;
    /** Reserved for future use. */
    uint8_t      au8Reserved[472];
} VciBlkMap, *PVciBlkMap;
#pragma pack()
AssertCompileSize(VciBlkMap, VCI_BLOCK_SIZE);

/** The magic which identifies a block map. */
#define VCI_BLKMAP_MAGIC   UINT32_C(0x4b4c4256) /* KLBV */
/** Current version. */
#define VCI_BLKMAP_VERSION UINT32_C(0x00000001)

/** Block bitmap entry */
typedef uint8_t VciBlkMapEnt;


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/**
 * Block range descriptor.
 */
typedef struct VCIBLKRANGEDESC
{
    /** Previous entry in the list. */
    struct VCIBLKRANGEDESC    *pPrev;
    /** Next entry in the list. */
    struct VCIBLKRANGEDESC    *pNext;
    /** Start address of the range. */
    uint64_t                   offAddrStart;
    /** Number of blocks in the range. */
    uint64_t                   cBlocks;
    /** Flag whether the range is free or allocated. */
    bool                       fFree;
} VCIBLKRANGEDESC, *PVCIBLKRANGEDESC;

/**
 * Block map for the cache image - in memory structure.
 */
typedef struct VCIBLKMAP
{
    /** Number of blocks the map manages. */
    uint64_t     cBlocks;
    /** Number of blocks allocated for metadata. */
    uint64_t     cBlocksAllocMeta;
    /** Number of blocks allocated for actual cached data. */
    uint64_t     cBlocksAllocData;
    /** Number of free blocks. */
    uint64_t     cBlocksFree;

    /** Pointer to the head of the block range list. */
    PVCIBLKRANGEDESC pRangesHead;
    /** Pointer to the tail of the block range list. */
    PVCIBLKRANGEDESC pRangesTail;

} VCIBLKMAP;
/** Pointer to a block map. */
typedef VCIBLKMAP *PVCIBLKMAP;

/**
 * B+-Tree node header.
 */
typedef struct VCITREENODE
{
    /** Type of the node (VCI_TREE_NODE_TYPE_*). */
    uint8_t             u8Type;
    /** Block address where the node is stored. */
    uint64_t            u64BlockAddr;
    /** Pointer to the parent. */
    struct VCITREENODE *pParent;
} VCITREENODE, *PVCITREENODE;

/**
 * B+-Tree node pointer.
 */
typedef struct VCITREENODEPTR
{
    /** Flag whether the node is in memory or still on the disk. */
    bool         fInMemory;
    /** Type dependent data. */
    union
    {
        /** Pointer to a in memory node. */
        PVCITREENODE pNode;
        /** Start block address of the node. */
        uint64_t     offAddrBlockNode;
    } u;
} VCITREENODEPTR, *PVCITREENODEPTR;

/**
 * Internal node.
 */
typedef struct VCINODEINTERNAL
{
    /** First block of cached data the internal node represents. */
    uint64_t       u64BlockOffset;
    /** Number of blocks the internal node represents. */
    uint32_t       u32Blocks;
    /** Pointer to the child node. */
    VCITREENODEPTR PtrChild;
} VCINODEINTERNAL, *PVCINODEINTERNAL;

/**
 * A in memory internal B+-tree node.
 */
typedef struct VCITREENODEINT
{
    /** Node core. */
    VCITREENODE     Core;
    /** Number of used nodes. */
    unsigned        cUsedNodes;
    /** Array of internal nodes. */
    VCINODEINTERNAL aIntNodes[VCI_TREE_INTERNAL_NODES_PER_NODE];
} VCITREENODEINT, *PVCITREENODEINT;

/**
 * A in memory cache extent.
 */
typedef struct VCICACHEEXTENT
{
    /** First block of cached data the extent represents. */
    uint64_t    u64BlockOffset;
    /** Number of blocks the extent represents. */
    uint32_t    u32Blocks;
    /** First block in the image where the data is stored. */
    uint64_t    u64BlockAddr;
} VCICACHEEXTENT, *PVCICACHEEXTENT;

/**
 * A in memory leaf B+-tree node.
 */
typedef struct VCITREENODELEAF
{
    /** Node core. */
    VCITREENODE             Core;
    /** Next leaf node in the list. */
    struct VCITREENODELEAF *pNext;
    /** Number of used nodes. */
    unsigned                cUsedNodes;
    /** The extents in the node. */
    VCICACHEEXTENT          aExtents[VCI_TREE_EXTENTS_PER_NODE];
} VCITREENODELEAF, *PVCITREENODELEAF;

/**
 * VCI image data structure.
 */
typedef struct VCICACHE
{
    /** Image name. */
    const char       *pszFilename;
    /** Storage handle. */
    PVDIOSTORAGE      pStorage;

    /** Pointer to the per-disk VD interface list. */
    PVDINTERFACE      pVDIfsDisk;
    /** Pointer to the per-image VD interface list. */
    PVDINTERFACE      pVDIfsImage;
    /** Error interface. */
    PVDINTERFACEERROR pIfError;
    /** I/O interface. */
    PVDINTERFACEIOINT pIfIo;

    /** Open flags passed by VBoxHD layer. */
    unsigned          uOpenFlags;
    /** Image flags defined during creation or determined during open. */
    unsigned          uImageFlags;
    /** Total size of the image. */
    uint64_t          cbSize;

    /** Offset of the B+-Tree in the image in bytes. */
    uint64_t          offTreeRoot;
    /** Pointer to the root node of the B+-Tree. */
    PVCITREENODE      pRoot;
    /** Offset to the block allocation bitmap in bytes. */
    uint64_t          offBlksBitmap;
    /** Block map. */
    PVCIBLKMAP        pBlkMap;
} VCICACHE, *PVCICACHE;

/** No block free in bitmap error code. */
#define VERR_VCI_NO_BLOCKS_FREE (-65536)

/** Flags for the block map allocator. */
#define VCIBLKMAP_ALLOC_DATA 0
#define VCIBLKMAP_ALLOC_META RT_BIT(0)
#define VCIBLKMAP_ALLOC_MASK 0x1


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/** NULL-terminated array of supported file extensions. */
static const char *const s_apszVciFileExtensions[] =
{
    "vci",
    NULL
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Internal. Flush image data to disk.
 */
static int vciFlushImage(PVCICACHE pCache)
{
    int rc = VINF_SUCCESS;

    if (   pCache->pStorage
        && !(pCache->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        rc = vdIfIoIntFileFlushSync(pCache->pIfIo, pCache->pStorage);
    }

    return rc;
}

/**
 * Internal. Free all allocated space for representing an image except pCache,
 * and optionally delete the image from disk.
 */
static int vciFreeImage(PVCICACHE pCache, bool fDelete)
{
    int rc = VINF_SUCCESS;

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pCache)
    {
        if (pCache->pStorage)
        {
            /* No point updating the file that is deleted anyway. */
            if (!fDelete)
                vciFlushImage(pCache);

            vdIfIoIntFileClose(pCache->pIfIo, pCache->pStorage);
            pCache->pStorage = NULL;
        }

        if (fDelete && pCache->pszFilename)
            vdIfIoIntFileDelete(pCache->pIfIo, pCache->pszFilename);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Creates a new block map which can manage the given number of blocks.
 *
 * The size of the bitmap is aligned to the VCI block size.
 *
 * @returns VBox status code.
 * @param   cBlocks      The number of blocks the bitmap can manage.
 * @param   ppBlkMap     Where to store the pointer to the block bitmap.
 * @param   pcBlkMap     Where to store the size of the block bitmap in blocks
 *                       needed on the disk.
 */
static int vciBlkMapCreate(uint64_t cBlocks, PVCIBLKMAP *ppBlkMap, uint32_t *pcBlkMap)
{
    int rc = VINF_SUCCESS;
    uint32_t cbBlkMap = RT_ALIGN_Z(cBlocks / sizeof(VciBlkMapEnt) / 8, VCI_BLOCK_SIZE);
    PVCIBLKMAP pBlkMap = (PVCIBLKMAP)RTMemAllocZ(sizeof(VCIBLKMAP));
    PVCIBLKRANGEDESC pFree   = (PVCIBLKRANGEDESC)RTMemAllocZ(sizeof(VCIBLKRANGEDESC));

    LogFlowFunc(("cBlocks=%u ppBlkMap=%#p pcBlkMap=%#p\n", cBlocks, ppBlkMap, pcBlkMap));

    if (pBlkMap && pFree)
    {
        pBlkMap->cBlocks          = cBlocks;
        pBlkMap->cBlocksAllocMeta = 0;
        pBlkMap->cBlocksAllocData = 0;
        pBlkMap->cBlocksFree      = cBlocks;

        pFree->pPrev = NULL;
        pFree->pNext = NULL;
        pFree->offAddrStart = 0;
        pFree->cBlocks      = cBlocks;
        pFree->fFree        = true;

        pBlkMap->pRangesHead = pFree;
        pBlkMap->pRangesTail = pFree;

        Assert(!((cbBlkMap + sizeof(VciBlkMap)) % VCI_BLOCK_SIZE));
        *ppBlkMap = pBlkMap;
        *pcBlkMap = VCI_BYTE2BLOCK(cbBlkMap + sizeof(VciBlkMap));
    }
    else
    {
        if (pBlkMap)
            RTMemFree(pBlkMap);
        if (pFree)
            RTMemFree(pFree);

        rc = VERR_NO_MEMORY;
    }

    LogFlowFunc(("returns rc=%Rrc cBlkMap=%u\n", rc, *pcBlkMap));
    return rc;
}

#if 0 /** @todo unsued vciBlkMapDestroy */
/**
 * Frees a block map.
 *
 * @param   pBlkMap         The block bitmap to destroy.
 */
static void vciBlkMapDestroy(PVCIBLKMAP pBlkMap)
{
    LogFlowFunc(("pBlkMap=%#p\n", pBlkMap));

    PVCIBLKRANGEDESC pRangeCur = pBlkMap->pRangesHead;

    while (pRangeCur)
    {
        PVCIBLKRANGEDESC pTmp = pRangeCur;

        RTMemFree(pTmp);

        pRangeCur = pRangeCur->pNext;
    }

    RTMemFree(pBlkMap);

    LogFlowFunc(("returns\n"));
}
#endif

/**
 * Loads the block map from the specified medium and creates all necessary
 * in memory structures to manage used and free blocks.
 *
 * @returns VBox status code.
 * @param   pStorage        Storage handle to read the block bitmap from.
 * @param   offBlkMap       Start of the block bitmap in blocks.
 * @param   cBlkMap         Size of the block bitmap on the disk in blocks.
 * @param   ppBlkMap        Where to store the block bitmap on success.
 */
static int vciBlkMapLoad(PVCICACHE pStorage, uint64_t offBlkMap, uint32_t cBlkMap, PVCIBLKMAP *ppBlkMap)
{
    int rc = VINF_SUCCESS;
    VciBlkMap BlkMap;

    LogFlowFunc(("pStorage=%#p offBlkMap=%llu cBlkMap=%u ppBlkMap=%#p\n",
                 pStorage, offBlkMap, cBlkMap, ppBlkMap));

    if (cBlkMap >= VCI_BYTE2BLOCK(sizeof(VciBlkMap)))
    {
        cBlkMap -= VCI_BYTE2BLOCK(sizeof(VciBlkMap));

        rc = vdIfIoIntFileReadSync(pStorage->pIfIo, pStorage->pStorage, offBlkMap,
                                   &BlkMap, VCI_BYTE2BLOCK(sizeof(VciBlkMap)));
        if (RT_SUCCESS(rc))
        {
            offBlkMap += VCI_BYTE2BLOCK(sizeof(VciBlkMap));

            BlkMap.u32Magic         = RT_LE2H_U32(BlkMap.u32Magic);
            BlkMap.u32Version       = RT_LE2H_U32(BlkMap.u32Version);
            BlkMap.cBlocks          = RT_LE2H_U32(BlkMap.cBlocks);
            BlkMap.cBlocksFree      = RT_LE2H_U32(BlkMap.cBlocksFree);
            BlkMap.cBlocksAllocMeta = RT_LE2H_U32(BlkMap.cBlocksAllocMeta);
            BlkMap.cBlocksAllocData = RT_LE2H_U32(BlkMap.cBlocksAllocData);

            if (   BlkMap.u32Magic == VCI_BLKMAP_MAGIC
                && BlkMap.u32Version == VCI_BLKMAP_VERSION
                && BlkMap.cBlocks == BlkMap.cBlocksFree + BlkMap.cBlocksAllocMeta + BlkMap.cBlocksAllocData
                && VCI_BYTE2BLOCK(BlkMap.cBlocks / 8) == cBlkMap)
            {
                PVCIBLKMAP pBlkMap = (PVCIBLKMAP)RTMemAllocZ(sizeof(VCIBLKMAP));
                if (pBlkMap)
                {
                    pBlkMap->cBlocks          = BlkMap.cBlocks;
                    pBlkMap->cBlocksFree      = BlkMap.cBlocksFree;
                    pBlkMap->cBlocksAllocMeta = BlkMap.cBlocksAllocMeta;
                    pBlkMap->cBlocksAllocData = BlkMap.cBlocksAllocData;

                    /* Load the bitmap and construct the range list. */
                    PVCIBLKRANGEDESC pRangeCur = (PVCIBLKRANGEDESC)RTMemAllocZ(sizeof(VCIBLKRANGEDESC));

                    if (pRangeCur)
                    {
                        uint8_t abBitmapBuffer[16 * _1K];
                        uint32_t cBlocksRead = 0;
                        uint64_t cBlocksLeft = VCI_BYTE2BLOCK(pBlkMap->cBlocks / 8);

                        cBlocksRead = RT_MIN(VCI_BYTE2BLOCK(sizeof(abBitmapBuffer)), cBlocksLeft);
                        rc = vdIfIoIntFileReadSync(pStorage->pIfIo, pStorage->pStorage,
                                                   offBlkMap, abBitmapBuffer,
                                                   cBlocksRead);

                        if (RT_SUCCESS(rc))
                        {
                            pRangeCur->fFree        = !(abBitmapBuffer[0] & 0x01);
                            pRangeCur->offAddrStart = 0;
                            pRangeCur->cBlocks      = 0;
                            pRangeCur->pNext        = NULL;
                            pRangeCur->pPrev        = NULL;
                            pBlkMap->pRangesHead = pRangeCur;
                            pBlkMap->pRangesTail = pRangeCur;
                        }
                        else
                            RTMemFree(pRangeCur);

                        while (   RT_SUCCESS(rc)
                               && cBlocksLeft)
                        {
                            int iBit = 0;
                            uint32_t cBits = VCI_BLOCK2BYTE(cBlocksRead) * 8;
                            uint32_t iBitPrev = 0xffffffff;

                            while (cBits)
                            {
                                if (pRangeCur->fFree)
                                {
                                    /* Check for the first set bit. */
                                    iBit = ASMBitNextSet(abBitmapBuffer, cBits, iBitPrev);
                                }
                                else
                                {
                                    /* Check for the first free bit. */
                                    iBit = ASMBitNextClear(abBitmapBuffer, cBits, iBitPrev);
                                }

                                if (iBit == -1)
                                {
                                    /* No change. */
                                    pRangeCur->cBlocks += cBits;
                                    cBits = 0;
                                }
                                else
                                {
                                    Assert((uint32_t)iBit < cBits);
                                    pRangeCur->cBlocks += iBit;

                                    /* Create a new range descriptor. */
                                    PVCIBLKRANGEDESC pRangeNew = (PVCIBLKRANGEDESC)RTMemAllocZ(sizeof(VCIBLKRANGEDESC));
                                    if (!pRangeNew)
                                    {
                                        rc = VERR_NO_MEMORY;
                                        break;
                                    }

                                    pRangeNew->fFree = !pRangeCur->fFree;
                                    pRangeNew->offAddrStart = pRangeCur->offAddrStart + pRangeCur->cBlocks;
                                    pRangeNew->cBlocks = 0;
                                    pRangeNew->pPrev = pRangeCur;
                                    pRangeCur->pNext = pRangeNew;
                                    pBlkMap->pRangesTail = pRangeNew;
                                    pRangeCur = pRangeNew;
                                    cBits -= iBit;
                                    iBitPrev = iBit;
                                }
                            }

                            cBlocksLeft -= cBlocksRead;
                            offBlkMap   += cBlocksRead;

                            if (   RT_SUCCESS(rc)
                                && cBlocksLeft)
                            {
                                /* Read next chunk. */
                                cBlocksRead = RT_MIN(VCI_BYTE2BLOCK(sizeof(abBitmapBuffer)), cBlocksLeft);
                                rc = vdIfIoIntFileReadSync(pStorage->pIfIo, pStorage->pStorage,
                                                           offBlkMap, abBitmapBuffer, cBlocksRead);
                            }
                        }
                    }
                    else
                        rc = VERR_NO_MEMORY;

                    if (RT_SUCCESS(rc))
                    {
                        *ppBlkMap = pBlkMap;
                        LogFlowFunc(("return success\n"));
                        return VINF_SUCCESS;
                    }

                    RTMemFree(pBlkMap);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else
                rc = VERR_VD_GEN_INVALID_HEADER;
        }
        else
            rc = VERR_VD_GEN_INVALID_HEADER;
    }
    else
        rc = VERR_VD_GEN_INVALID_HEADER;

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Saves the block map in the cache image. All necessary on disk structures
 * are written.
 *
 * @returns VBox status code.
 * @param   pBlkMap         The block bitmap to save.
 * @param   pStorage        Where the block bitmap should be written to.
 * @param   offBlkMap       Start of the block bitmap in blocks.
 * @param   cBlkMap         Size of the block bitmap on the disk in blocks.
 */
static int vciBlkMapSave(PVCIBLKMAP pBlkMap, PVCICACHE pStorage, uint64_t offBlkMap, uint32_t cBlkMap)
{
    int rc = VINF_SUCCESS;
    VciBlkMap BlkMap;

    LogFlowFunc(("pBlkMap=%#p pStorage=%#p offBlkMap=%llu cBlkMap=%u\n",
                 pBlkMap, pStorage, offBlkMap, cBlkMap));

    /* Make sure the number of blocks allocated for us match our expectations. */
    if (VCI_BYTE2BLOCK(pBlkMap->cBlocks / 8) + VCI_BYTE2BLOCK(sizeof(VciBlkMap)) == cBlkMap)
    {
        /* Setup the header */
        memset(&BlkMap, 0, sizeof(VciBlkMap));

        BlkMap.u32Magic         = RT_H2LE_U32(VCI_BLKMAP_MAGIC);
        BlkMap.u32Version       = RT_H2LE_U32(VCI_BLKMAP_VERSION);
        BlkMap.cBlocks          = RT_H2LE_U32(pBlkMap->cBlocks);
        BlkMap.cBlocksFree      = RT_H2LE_U32(pBlkMap->cBlocksFree);
        BlkMap.cBlocksAllocMeta = RT_H2LE_U32(pBlkMap->cBlocksAllocMeta);
        BlkMap.cBlocksAllocData = RT_H2LE_U32(pBlkMap->cBlocksAllocData);

        rc = vdIfIoIntFileWriteSync(pStorage->pIfIo, pStorage->pStorage, offBlkMap,
                                    &BlkMap, VCI_BYTE2BLOCK(sizeof(VciBlkMap)));
        if (RT_SUCCESS(rc))
        {
            uint8_t abBitmapBuffer[16*_1K];
            unsigned iBit = 0;
            PVCIBLKRANGEDESC pCur = pBlkMap->pRangesHead;

            offBlkMap += VCI_BYTE2BLOCK(sizeof(VciBlkMap));

            /* Write the descriptor ranges. */
            while (pCur)
            {
                uint64_t cBlocks = pCur->cBlocks;

                while (cBlocks)
                {
                    uint64_t cBlocksMax = RT_MIN(cBlocks, sizeof(abBitmapBuffer) * 8 - iBit);

                    if (pCur->fFree)
                        ASMBitClearRange(abBitmapBuffer, iBit, iBit + cBlocksMax);
                    else
                        ASMBitSetRange(abBitmapBuffer, iBit, iBit + cBlocksMax);

                    iBit    += cBlocksMax;
                    cBlocks -= cBlocksMax;

                    if (iBit == sizeof(abBitmapBuffer) * 8)
                    {
                        /* Buffer is full, write to file and reset. */
                        rc = vdIfIoIntFileWriteSync(pStorage->pIfIo, pStorage->pStorage,
                                                    offBlkMap, abBitmapBuffer,
                                                    VCI_BYTE2BLOCK(sizeof(abBitmapBuffer)));
                        if (RT_FAILURE(rc))
                            break;

                        offBlkMap += VCI_BYTE2BLOCK(sizeof(abBitmapBuffer));
                        iBit = 0;
                    }
                }

                pCur = pCur->pNext;
            }

            Assert(iBit % 8 == 0);

            if (RT_SUCCESS(rc) && iBit)
                rc = vdIfIoIntFileWriteSync(pStorage->pIfIo, pStorage->pStorage,
                                            offBlkMap, abBitmapBuffer, VCI_BYTE2BLOCK(iBit / 8));
        }
    }
    else
        rc = VERR_INTERNAL_ERROR; /** @todo Better error code. */

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

#if 0 /* unused */
/**
 * Finds the range block describing the given block address.
 *
 * @returns Pointer to the block range descriptor or NULL if none could be found.
 * @param   pBlkMap         The block bitmap to search on.
 * @param   offBlockAddr    The block address to search for.
 */
static PVCIBLKRANGEDESC vciBlkMapFindByBlock(PVCIBLKMAP pBlkMap, uint64_t offBlockAddr)
{
    PVCIBLKRANGEDESC pBlk = pBlkMap->pRangesHead;

    while (   pBlk
           && pBlk->offAddrStart < offBlockAddr)
        pBlk = pBlk->pNext;

    return pBlk;
}
#endif

/**
 * Allocates the given number of blocks in the bitmap and returns the start block address.
 *
 * @returns VBox status code.
 * @param   pBlkMap          The block bitmap to allocate the blocks from.
 * @param   cBlocks          How many blocks to allocate.
 * @param   fFlags           Allocation flags, comgination of VCIBLKMAP_ALLOC_*.
 * @param   poffBlockAddr    Where to store the start address of the allocated region.
 */
static int vciBlkMapAllocate(PVCIBLKMAP pBlkMap, uint32_t cBlocks, uint32_t fFlags,
                             uint64_t *poffBlockAddr)
{
    PVCIBLKRANGEDESC pBestFit = NULL;
    PVCIBLKRANGEDESC pCur = NULL;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pBlkMap=%#p cBlocks=%u poffBlockAddr=%#p\n",
                 pBlkMap, cBlocks, poffBlockAddr));

    pCur = pBlkMap->pRangesHead;

    while (pCur)
    {
        if (   pCur->fFree
            && pCur->cBlocks >= cBlocks)
        {
            if (   !pBestFit
                || pCur->cBlocks < pBestFit->cBlocks)
            {
                pBestFit = pCur;
                /* Stop searching if the size is matching exactly. */
                if (pBestFit->cBlocks == cBlocks)
                    break;
            }
        }
        pCur = pCur->pNext;
    }

    Assert(!pBestFit || pBestFit->fFree);

    if (pBestFit)
    {
        pBestFit->fFree = false;

        if (pBestFit->cBlocks > cBlocks)
        {
            /* Create a new free block. */
            PVCIBLKRANGEDESC pFree = (PVCIBLKRANGEDESC)RTMemAllocZ(sizeof(VCIBLKRANGEDESC));

            if (pFree)
            {
                pFree->fFree = true;
                pFree->cBlocks = pBestFit->cBlocks - cBlocks;
                pBestFit->cBlocks -= pFree->cBlocks;
                pFree->offAddrStart = pBestFit->offAddrStart + cBlocks;

                /* Link into the list. */
                pFree->pNext = pBestFit->pNext;
                pBestFit->pNext = pFree;
                pFree->pPrev    = pBestFit;
                if (!pFree->pNext)
                    pBlkMap->pRangesTail = pFree;

                *poffBlockAddr = pBestFit->offAddrStart;
            }
            else
            {
                rc = VERR_NO_MEMORY;
                pBestFit->fFree = true;
            }
        }
    }
    else
        rc = VERR_VCI_NO_BLOCKS_FREE;

    if (RT_SUCCESS(rc))
    {
        if ((fFlags & VCIBLKMAP_ALLOC_MASK) == VCIBLKMAP_ALLOC_DATA)
            pBlkMap->cBlocksAllocMeta += cBlocks;
        else
            pBlkMap->cBlocksAllocData += cBlocks;

        pBlkMap->cBlocksFree -= cBlocks;
    }

    LogFlowFunc(("returns rc=%Rrc offBlockAddr=%llu\n", rc, *poffBlockAddr));
    return rc;
}

#if 0 /* unused */
/**
 * Try to extend the space of an already allocated block.
 *
 * @returns VBox status code.
 * @param   pBlkMap          The block bitmap to allocate the blocks from.
 * @param   cBlocksNew       How many blocks the extended block should have.
 * @param   offBlockAddrOld  The start address of the block to reallocate.
 * @param   poffBlockAddr    Where to store the start address of the allocated region.
 */
static int vciBlkMapRealloc(PVCIBLKMAP pBlkMap, uint32_t cBlocksNew, uint64_t offBlockAddrOld,
                            uint64_t *poffBlockAddr)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pBlkMap=%#p cBlocksNew=%u offBlockAddrOld=%llu poffBlockAddr=%#p\n",
                 pBlkMap, cBlocksNew, offBlockAddrOld, poffBlockAddr));

    AssertMsgFailed(("Implement\n"));
    RT_NOREF4(pBlkMap, cBlocksNew, offBlockAddrOld, poffBlockAddr);

    LogFlowFunc(("returns rc=%Rrc offBlockAddr=%llu\n", rc, *poffBlockAddr));
    return rc;
}
#endif /* unused */

#if 0 /* unused */
/**
 * Frees a range of blocks.
 *
 * @param   pBlkMap          The block bitmap.
 * @param   offBlockAddr     Address of the first block to free.
 * @param   cBlocks          How many blocks to free.
 * @param   fFlags           Allocation flags, comgination of VCIBLKMAP_ALLOC_*.
 */
static void vciBlkMapFree(PVCIBLKMAP pBlkMap, uint64_t offBlockAddr, uint32_t cBlocks,
                          uint32_t fFlags)
{
    PVCIBLKRANGEDESC pBlk;

    LogFlowFunc(("pBlkMap=%#p offBlockAddr=%llu cBlocks=%u\n",
                 pBlkMap, offBlockAddr, cBlocks));

    while (cBlocks)
    {
        pBlk = vciBlkMapFindByBlock(pBlkMap, offBlockAddr);
        AssertPtr(pBlk);

        /* Easy case, the whole block is freed. */
        if (   pBlk->offAddrStart == offBlockAddr
            && pBlk->cBlocks <= cBlocks)
        {
            pBlk->fFree = true;
            cBlocks      -= pBlk->cBlocks;
            offBlockAddr += pBlk->cBlocks;

            /* Check if it is possible to merge free blocks. */
            if (   pBlk->pPrev
                && pBlk->pPrev->fFree)
            {
                PVCIBLKRANGEDESC pBlkPrev = pBlk->pPrev;

                Assert(pBlkPrev->offAddrStart + pBlkPrev->cBlocks == pBlk->offAddrStart);
                pBlkPrev->cBlocks += pBlk->cBlocks;
                pBlkPrev->pNext = pBlk->pNext;
                if (pBlk->pNext)
                    pBlk->pNext->pPrev = pBlkPrev;
                else
                    pBlkMap->pRangesTail = pBlkPrev;

                RTMemFree(pBlk);
                pBlk = pBlkPrev;
            }

            /* Now the one to the right. */
            if (   pBlk->pNext
                && pBlk->pNext->fFree)
            {
                PVCIBLKRANGEDESC pBlkNext = pBlk->pNext;

                Assert(pBlk->offAddrStart + pBlk->cBlocks == pBlkNext->offAddrStart);
                pBlk->cBlocks += pBlkNext->cBlocks;
                pBlk->pNext = pBlkNext->pNext;
                if (pBlkNext->pNext)
                    pBlkNext->pNext->pPrev = pBlk;
                else
                    pBlkMap->pRangesTail = pBlk;

                RTMemFree(pBlkNext);
            }
        }
        else
        {
            /* The block is intersecting. */
            AssertMsgFailed(("TODO\n"));
        }
    }

    if ((fFlags & VCIBLKMAP_ALLOC_MASK) == VCIBLKMAP_ALLOC_DATA)
        pBlkMap->cBlocksAllocMeta -= cBlocks;
    else
        pBlkMap->cBlocksAllocData -= cBlocks;

    pBlkMap->cBlocksFree += cBlocks;

    LogFlowFunc(("returns\n"));
}
#endif /* unused */

/**
 * Converts a tree node from the image to the in memory structure.
 *
 * @returns Pointer to the in memory tree node.
 * @param   offBlockAddrNode    Block address of the node.
 * @param   pNodeImage          Pointer to the image representation of the node.
 */
static PVCITREENODE vciTreeNodeImage2Host(uint64_t offBlockAddrNode, PVciTreeNode pNodeImage)
{
    PVCITREENODE pNode = NULL;

    if (pNodeImage->u8Type == VCI_TREE_NODE_TYPE_LEAF)
    {
        PVCITREENODELEAF pLeaf = (PVCITREENODELEAF)RTMemAllocZ(sizeof(VCITREENODELEAF));

        if (pLeaf)
        {
            PVciCacheExtent pExtent = (PVciCacheExtent)&pNodeImage->au8Data[0];

            pLeaf->Core.u8Type = VCI_TREE_NODE_TYPE_LEAF;

            for (unsigned idx = 0; idx < RT_ELEMENTS(pLeaf->aExtents); idx++)
            {
                pLeaf->aExtents[idx].u64BlockOffset = RT_LE2H_U64(pExtent->u64BlockOffset);
                pLeaf->aExtents[idx].u32Blocks      = RT_LE2H_U32(pExtent->u32Blocks);
                pLeaf->aExtents[idx].u64BlockAddr   = RT_LE2H_U64(pExtent->u64BlockAddr);
                pExtent++;

                if (   pLeaf->aExtents[idx].u32Blocks
                    && pLeaf->aExtents[idx].u64BlockAddr)
                    pLeaf->cUsedNodes++;
            }

            pNode = &pLeaf->Core;
        }
    }
    else if (pNodeImage->u8Type == VCI_TREE_NODE_TYPE_INTERNAL)
    {
        PVCITREENODEINT pInt = (PVCITREENODEINT)RTMemAllocZ(sizeof(VCITREENODEINT));

        if (pInt)
        {
            PVciTreeNodeInternal pIntImage = (PVciTreeNodeInternal)&pNodeImage->au8Data[0];

            pInt->Core.u8Type = VCI_TREE_NODE_TYPE_INTERNAL;

            for (unsigned idx = 0; idx < RT_ELEMENTS(pInt->aIntNodes); idx++)
            {
                pInt->aIntNodes[idx].u64BlockOffset              = RT_LE2H_U64(pIntImage->u64BlockOffset);
                pInt->aIntNodes[idx].u32Blocks                   = RT_LE2H_U32(pIntImage->u32Blocks);
                pInt->aIntNodes[idx].PtrChild.fInMemory          = false;
                pInt->aIntNodes[idx].PtrChild.u.offAddrBlockNode = RT_LE2H_U64(pIntImage->u64ChildAddr);
                pIntImage++;

                if (   pInt->aIntNodes[idx].u32Blocks
                    && pInt->aIntNodes[idx].PtrChild.u.offAddrBlockNode)
                    pInt->cUsedNodes++;
            }

            pNode = &pInt->Core;
        }
    }
    else
        AssertMsgFailed(("Invalid node type %d\n", pNodeImage->u8Type));

    if (pNode)
        pNode->u64BlockAddr = offBlockAddrNode;

    return pNode;
}

/**
 * Looks up the cache extent for the given virtual block address.
 *
 * @returns Pointer to the cache extent or NULL if none could be found.
 * @param   pCache         The cache image instance.
 * @param   offBlockOffset The block offset to search for.
 * @param   ppNextBestFit  Where to store the pointer to the next best fit
 *                         cache extent above offBlockOffset if existing. - Optional
 *                         This is always filled if possible even if the function returns NULL.
 */
static PVCICACHEEXTENT vciCacheExtentLookup(PVCICACHE pCache, uint64_t offBlockOffset,
                                            PVCICACHEEXTENT *ppNextBestFit)
{
    int rc = VINF_SUCCESS;
    PVCICACHEEXTENT pExtent = NULL;
    PVCITREENODE pNodeCur = pCache->pRoot;

    while (   RT_SUCCESS(rc)
           && pNodeCur
           && pNodeCur->u8Type != VCI_TREE_NODE_TYPE_LEAF)
    {
        PVCITREENODEINT pNodeInt = (PVCITREENODEINT)pNodeCur;

        Assert(pNodeCur->u8Type == VCI_TREE_NODE_TYPE_INTERNAL);

        /* Search for the correct internal node. */
        unsigned idxMin = 0;
        unsigned idxMax = pNodeInt->cUsedNodes;
        unsigned idxCur = pNodeInt->cUsedNodes / 2;

        while (idxMin < idxMax)
        {
            PVCINODEINTERNAL pInt = &pNodeInt->aIntNodes[idxCur];

            /* Determine the search direction. */
            if (offBlockOffset < pInt->u64BlockOffset)
            {
                /* Search left from the current extent. */
                idxMax = idxCur;
            }
            else if (offBlockOffset >= pInt->u64BlockOffset + pInt->u32Blocks)
            {
                /* Search right from the current extent. */
                idxMin = idxCur;
            }
            else
            {
                /* The block lies in the node, stop searching. */
                if (pInt->PtrChild.fInMemory)
                    pNodeCur = pInt->PtrChild.u.pNode;
                else
                {
                    PVCITREENODE pNodeNew;
                    VciTreeNode NodeTree;

                    /* Read from disk and add to the tree. */
                    rc = vdIfIoIntFileReadSync(pCache->pIfIo, pCache->pStorage,
                                               VCI_BLOCK2BYTE(pInt->PtrChild.u.offAddrBlockNode),
                                               &NodeTree, sizeof(NodeTree));
                    AssertRC(rc);

                    pNodeNew = vciTreeNodeImage2Host(pInt->PtrChild.u.offAddrBlockNode, &NodeTree);
                    if (pNodeNew)
                    {
                        /* Link to the parent. */
                        pInt->PtrChild.fInMemory = true;
                        pInt->PtrChild.u.pNode = pNodeNew;
                        pNodeNew->pParent = pNodeCur;
                        pNodeCur = pNodeNew;
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
                break;
            }

            idxCur = idxMin + (idxMax - idxMin) / 2;
        }
    }

    if (   RT_SUCCESS(rc)
        && pNodeCur)
    {
        PVCITREENODELEAF pLeaf = (PVCITREENODELEAF)pNodeCur;
        Assert(pNodeCur->u8Type == VCI_TREE_NODE_TYPE_LEAF);

        /* Search the range. */
        unsigned idxMin = 0;
        unsigned idxMax = pLeaf->cUsedNodes;
        unsigned idxCur = pLeaf->cUsedNodes / 2;

        while (idxMin < idxMax)
        {
            PVCICACHEEXTENT pExtentCur = &pLeaf->aExtents[idxCur];

            /* Determine the search direction. */
            if (offBlockOffset < pExtentCur->u64BlockOffset)
            {
                /* Search left from the current extent. */
                idxMax = idxCur;
            }
            else if (offBlockOffset >= pExtentCur->u64BlockOffset + pExtentCur->u32Blocks)
            {
                /* Search right from the current extent. */
                idxMin = idxCur;
            }
            else
            {
                /* We found the extent, stop searching. */
                pExtent = pExtentCur;
                break;
            }

            idxCur = idxMin + (idxMax - idxMin) / 2;
        }

        /* Get the next best fit extent if it exists. */
        if (ppNextBestFit)
        {
            if (idxCur < pLeaf->cUsedNodes - 1)
                *ppNextBestFit = &pLeaf->aExtents[idxCur + 1];
            else
            {
                /*
                 * Go up the tree and find the best extent
                 * in the leftmost tree of the child subtree to the right.
                 */
                PVCITREENODEINT pInt = (PVCITREENODEINT)pLeaf->Core.pParent;

                while (pInt)
                {

                }
            }
        }
    }

    return pExtent;
}

/**
 * Internal: Open an image, constructing all necessary data structures.
 */
static int vciOpenImage(PVCICACHE pCache, unsigned uOpenFlags)
{
    VciHdr Hdr;
    uint64_t cbFile;
    int rc;

    pCache->uOpenFlags = uOpenFlags;

    pCache->pIfError = VDIfErrorGet(pCache->pVDIfsDisk);
    pCache->pIfIo = VDIfIoIntGet(pCache->pVDIfsImage);
    AssertPtrReturn(pCache->pIfIo, VERR_INVALID_PARAMETER);

    /*
     * Open the image.
     */
    rc = vdIfIoIntFileOpen(pCache->pIfIo, pCache->pszFilename,
                           VDOpenFlagsToFileOpenFlags(uOpenFlags,
                                                      false /* fCreate */),
                           &pCache->pStorage);
    if (RT_FAILURE(rc))
    {
        /* Do NOT signal an appropriate error here, as the VD layer has the
         * choice of retrying the open if it failed. */
        goto out;
    }

    rc = vdIfIoIntFileGetSize(pCache->pIfIo, pCache->pStorage, &cbFile);
    if (RT_FAILURE(rc) || cbFile < sizeof(VciHdr))
    {
        rc = VERR_VD_GEN_INVALID_HEADER;
        goto out;
    }

    rc = vdIfIoIntFileReadSync(pCache->pIfIo, pCache->pStorage, 0, &Hdr,
                               VCI_BYTE2BLOCK(sizeof(Hdr)));
    if (RT_FAILURE(rc))
    {
        rc = VERR_VD_GEN_INVALID_HEADER;
        goto out;
    }

    Hdr.u32Signature = RT_LE2H_U32(Hdr.u32Signature);
    Hdr.u32Version   = RT_LE2H_U32(Hdr.u32Version);
    Hdr.cBlocksCache = RT_LE2H_U64(Hdr.cBlocksCache);
    Hdr.u32CacheType = RT_LE2H_U32(Hdr.u32CacheType);
    Hdr.offTreeRoot  = RT_LE2H_U64(Hdr.offTreeRoot);
    Hdr.offBlkMap    = RT_LE2H_U64(Hdr.offBlkMap);
    Hdr.cBlkMap      = RT_LE2H_U32(Hdr.cBlkMap);

    if (   Hdr.u32Signature == VCI_HDR_SIGNATURE
        && Hdr.u32Version == VCI_HDR_VERSION)
    {
        pCache->offTreeRoot   = Hdr.offTreeRoot;
        pCache->offBlksBitmap = Hdr.offBlkMap;

        /* Load the block map. */
        rc = vciBlkMapLoad(pCache, pCache->offBlksBitmap, Hdr.cBlkMap, &pCache->pBlkMap);
        if (RT_SUCCESS(rc))
        {
            /* Load the first tree node. */
            VciTreeNode RootNode;

            rc = vdIfIoIntFileReadSync(pCache->pIfIo, pCache->pStorage,
                                       pCache->offTreeRoot, &RootNode,
                                       VCI_BYTE2BLOCK(sizeof(VciTreeNode)));
            if (RT_SUCCESS(rc))
            {
                pCache->pRoot = vciTreeNodeImage2Host(pCache->offTreeRoot, &RootNode);
                if (!pCache->pRoot)
                    rc = VERR_NO_MEMORY;
            }
        }
    }
    else
        rc = VERR_VD_GEN_INVALID_HEADER;

out:
    if (RT_FAILURE(rc))
        vciFreeImage(pCache, false);
    return rc;
}

/**
 * Internal: Create a vci image.
 */
static int vciCreateImage(PVCICACHE pCache, uint64_t cbSize,
                          unsigned uImageFlags, const char *pszComment,
                          unsigned uOpenFlags, PFNVDPROGRESS pfnProgress,
                          void *pvUser, unsigned uPercentStart,
                          unsigned uPercentSpan)
{
    RT_NOREF1(pszComment);
    VciHdr Hdr;
    VciTreeNode NodeRoot;
    int rc;
    uint64_t cBlocks = cbSize / VCI_BLOCK_SIZE; /* Size of the cache in blocks. */

    pCache->uImageFlags = uImageFlags;
    pCache->uOpenFlags = uOpenFlags & ~VD_OPEN_FLAGS_READONLY;

    pCache->pIfError = VDIfErrorGet(pCache->pVDIfsDisk);
    pCache->pIfIo = VDIfIoIntGet(pCache->pVDIfsImage);
    AssertPtrReturn(pCache->pIfIo, VERR_INVALID_PARAMETER);

    if (uImageFlags & VD_IMAGE_FLAGS_DIFF)
    {
        rc = vdIfError(pCache->pIfError, VERR_VD_RAW_INVALID_TYPE, RT_SRC_POS, N_("VCI: cannot create diff image '%s'"), pCache->pszFilename);
        return rc;
    }

    do
    {
        /* Create image file. */
        rc = vdIfIoIntFileOpen(pCache->pIfIo, pCache->pszFilename,
                               VDOpenFlagsToFileOpenFlags(uOpenFlags & ~VD_OPEN_FLAGS_READONLY,
                                                          true /* fCreate */),
                               &pCache->pStorage);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot create image '%s'"), pCache->pszFilename);
            break;
        }

        /* Allocate block bitmap. */
        uint32_t cBlkMap = 0;
        rc = vciBlkMapCreate(cBlocks, &pCache->pBlkMap, &cBlkMap);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot create block bitmap '%s'"), pCache->pszFilename);
            break;
        }

        /*
         * Allocate space for the header in the block bitmap.
         * Because the block map is empty the header has to start at block 0
         */
        uint64_t offHdr = 0;
        rc = vciBlkMapAllocate(pCache->pBlkMap, VCI_BYTE2BLOCK(sizeof(VciHdr)), VCIBLKMAP_ALLOC_META, &offHdr);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot allocate space for header in block bitmap '%s'"), pCache->pszFilename);
            break;
        }

        Assert(offHdr == 0);

        /*
         * Allocate space for the block map itself.
         */
        uint64_t offBlkMap = 0;
        rc = vciBlkMapAllocate(pCache->pBlkMap, cBlkMap, VCIBLKMAP_ALLOC_META, &offBlkMap);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot allocate space for block map in block map '%s'"), pCache->pszFilename);
            break;
        }

        /*
         * Allocate space for the tree root node.
         */
        uint64_t offTreeRoot = 0;
        rc = vciBlkMapAllocate(pCache->pBlkMap, VCI_BYTE2BLOCK(sizeof(VciTreeNode)), VCIBLKMAP_ALLOC_META, &offTreeRoot);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot allocate space for block map in block map '%s'"), pCache->pszFilename);
            break;
        }

        /*
         * Allocate the in memory root node.
         */
        pCache->pRoot = (PVCITREENODE)RTMemAllocZ(sizeof(VCITREENODELEAF));
        if (!pCache->pRoot)
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot allocate B+-Tree root pointer '%s'"), pCache->pszFilename);
            break;
        }

        pCache->pRoot->u8Type = VCI_TREE_NODE_TYPE_LEAF;
        /* Rest remains 0 as the tree is still empty. */

        /*
         * Now that we are here we have all the basic structures and know where to place them in the image.
         * It's time to write it now.
         */

        /* Setup the header. */
        memset(&Hdr, 0, sizeof(VciHdr));
        Hdr.u32Signature     = RT_H2LE_U32(VCI_HDR_SIGNATURE);
        Hdr.u32Version       = RT_H2LE_U32(VCI_HDR_VERSION);
        Hdr.cBlocksCache     = RT_H2LE_U64(cBlocks);
        Hdr.fUncleanShutdown = VCI_HDR_UNCLEAN_SHUTDOWN;
        Hdr.u32CacheType     = uImageFlags & VD_IMAGE_FLAGS_FIXED
                               ? RT_H2LE_U32(VCI_HDR_CACHE_TYPE_FIXED)
                               : RT_H2LE_U32(VCI_HDR_CACHE_TYPE_DYNAMIC);
        Hdr.offTreeRoot      = RT_H2LE_U64(offTreeRoot);
        Hdr.offBlkMap        = RT_H2LE_U64(offBlkMap);
        Hdr.cBlkMap          = RT_H2LE_U32(cBlkMap);

        rc = vdIfIoIntFileWriteSync(pCache->pIfIo, pCache->pStorage, offHdr, &Hdr,
                                    VCI_BYTE2BLOCK(sizeof(VciHdr)));
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot write header '%s'"), pCache->pszFilename);
            break;
        }

        rc = vciBlkMapSave(pCache->pBlkMap, pCache, offBlkMap, cBlkMap);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot write block map '%s'"), pCache->pszFilename);
            break;
        }

        /* Setup the root tree. */
        memset(&NodeRoot, 0, sizeof(VciTreeNode));
        NodeRoot.u8Type = VCI_TREE_NODE_TYPE_LEAF;

        rc = vdIfIoIntFileWriteSync(pCache->pIfIo, pCache->pStorage, offTreeRoot,
                                    &NodeRoot, VCI_BYTE2BLOCK(sizeof(VciTreeNode)));
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot write root node '%s'"), pCache->pszFilename);
            break;
        }

        rc = vciFlushImage(pCache);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pCache->pIfError, rc, RT_SRC_POS, N_("VCI: cannot flush '%s'"), pCache->pszFilename);
            break;
        }

        pCache->cbSize = cbSize;

    } while (0);

    if (RT_SUCCESS(rc) && pfnProgress)
        pfnProgress(pvUser, uPercentStart + uPercentSpan);

    if (RT_FAILURE(rc))
        vciFreeImage(pCache, rc != VERR_ALREADY_EXISTS);
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnProbe */
static DECLCALLBACK(int) vciProbe(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                                  PVDINTERFACE pVDIfsImage)
{
    RT_NOREF1(pVDIfsDisk);
    VciHdr Hdr;
    PVDIOSTORAGE pStorage = NULL;
    uint64_t cbFile;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pszFilename=\"%s\"\n", pszFilename));

    PVDINTERFACEIOINT pIfIo = VDIfIoIntGet(pVDIfsImage);
    AssertPtrReturn(pIfIo, VERR_INVALID_PARAMETER);

    rc = vdIfIoIntFileOpen(pIfIo, pszFilename,
                           VDOpenFlagsToFileOpenFlags(VD_OPEN_FLAGS_READONLY,
                                                      false /* fCreate */),
                           &pStorage);
    if (RT_FAILURE(rc))
        goto out;

    rc = vdIfIoIntFileGetSize(pIfIo, pStorage, &cbFile);
    if (RT_FAILURE(rc) || cbFile < sizeof(VciHdr))
    {
        rc = VERR_VD_GEN_INVALID_HEADER;
        goto out;
    }

    rc = vdIfIoIntFileReadSync(pIfIo, pStorage, 0, &Hdr, sizeof(Hdr));
    if (RT_FAILURE(rc))
    {
        rc = VERR_VD_GEN_INVALID_HEADER;
        goto out;
    }

    Hdr.u32Signature = RT_LE2H_U32(Hdr.u32Signature);
    Hdr.u32Version   = RT_LE2H_U32(Hdr.u32Version);
    Hdr.cBlocksCache = RT_LE2H_U64(Hdr.cBlocksCache);
    Hdr.u32CacheType = RT_LE2H_U32(Hdr.u32CacheType);
    Hdr.offTreeRoot  = RT_LE2H_U64(Hdr.offTreeRoot);
    Hdr.offBlkMap    = RT_LE2H_U64(Hdr.offBlkMap);
    Hdr.cBlkMap      = RT_LE2H_U32(Hdr.cBlkMap);

    if (   Hdr.u32Signature == VCI_HDR_SIGNATURE
        && Hdr.u32Version == VCI_HDR_VERSION)
        rc = VINF_SUCCESS;
    else
        rc = VERR_VD_GEN_INVALID_HEADER;

out:
    if (pStorage)
        vdIfIoIntFileClose(pIfIo, pStorage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnOpen */
static DECLCALLBACK(int) vciOpen(const char *pszFilename, unsigned uOpenFlags,
                                 PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                 void **ppBackendData)
{
    LogFlowFunc(("pszFilename=\"%s\" uOpenFlags=%#x pVDIfsDisk=%#p pVDIfsImage=%#p ppBackendData=%#p\n", pszFilename, uOpenFlags, pVDIfsDisk, pVDIfsImage, ppBackendData));
    int rc;
    PVCICACHE pCache;

    /* Check open flags. All valid flags are supported. */
    if (uOpenFlags & ~VD_OPEN_FLAGS_MASK)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Check remaining arguments. */
    if (   !RT_VALID_PTR(pszFilename)
        || !*pszFilename)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }


    pCache = (PVCICACHE)RTMemAllocZ(sizeof(VCICACHE));
    if (!pCache)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pCache->pszFilename = pszFilename;
    pCache->pStorage = NULL;
    pCache->pVDIfsDisk = pVDIfsDisk;
    pCache->pVDIfsImage = pVDIfsImage;

    rc = vciOpenImage(pCache, uOpenFlags);
    if (RT_SUCCESS(rc))
        *ppBackendData = pCache;
    else
        RTMemFree(pCache);

out:
    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnCreate */
static DECLCALLBACK(int) vciCreate(const char *pszFilename, uint64_t cbSize,
                                   unsigned uImageFlags, const char *pszComment,
                                   PCRTUUID pUuid, unsigned uOpenFlags,
                                   unsigned uPercentStart, unsigned uPercentSpan,
                                   PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                   PVDINTERFACE pVDIfsOperation, void **ppBackendData)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" Uuid=%RTuuid uOpenFlags=%#x uPercentStart=%u uPercentSpan=%u pVDIfsDisk=%#p pVDIfsImage=%#p pVDIfsOperation=%#p ppBackendData=%#p",
                 pszFilename, cbSize, uImageFlags, pszComment, pUuid, uOpenFlags, uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, ppBackendData));
    int rc;
    PVCICACHE pCache;

    PFNVDPROGRESS pfnProgress = NULL;
    void *pvUser = NULL;
    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);
    if (pIfProgress)
    {
        pfnProgress = pIfProgress->pfnProgress;
        pvUser = pIfProgress->Core.pvUser;
    }

    /* Check open flags. All valid flags are supported. */
    if (uOpenFlags & ~VD_OPEN_FLAGS_MASK)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Check remaining arguments. */
    if (   !RT_VALID_PTR(pszFilename)
        || !*pszFilename)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    pCache = (PVCICACHE)RTMemAllocZ(sizeof(VCICACHE));
    if (!pCache)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pCache->pszFilename = pszFilename;
    pCache->pStorage = NULL;
    pCache->pVDIfsDisk = pVDIfsDisk;
    pCache->pVDIfsImage = pVDIfsImage;

    rc = vciCreateImage(pCache, cbSize, uImageFlags, pszComment, uOpenFlags,
                        pfnProgress, pvUser, uPercentStart, uPercentSpan);
    if (RT_SUCCESS(rc))
    {
        /* So far the image is opened in read/write mode. Make sure the
         * image is opened in read-only mode if the caller requested that. */
        if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            vciFreeImage(pCache, false);
            rc = vciOpenImage(pCache, uOpenFlags);
            if (RT_FAILURE(rc))
            {
                RTMemFree(pCache);
                goto out;
            }
        }
        *ppBackendData = pCache;
    }
    else
        RTMemFree(pCache);

out:
    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnClose */
static DECLCALLBACK(int) vciClose(void *pBackendData, bool fDelete)
{
    LogFlowFunc(("pBackendData=%#p fDelete=%d\n", pBackendData, fDelete));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc;

    rc = vciFreeImage(pCache, fDelete);
    RTMemFree(pCache);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnRead */
static DECLCALLBACK(int) vciRead(void *pBackendData, uint64_t uOffset, size_t cbToRead,
                                 PVDIOCTX pIoCtx, size_t *pcbActuallyRead)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu cbToRead=%zu pIoCtx=%#p pcbActuallyRead=%#p\n",
                 pBackendData, uOffset, cbToRead, pIoCtx, pcbActuallyRead));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc = VINF_SUCCESS;
    PVCICACHEEXTENT pExtent;
    uint64_t cBlocksToRead = VCI_BYTE2BLOCK(cbToRead);
    uint64_t offBlockAddr  = VCI_BYTE2BLOCK(uOffset);

    AssertPtr(pCache);
    Assert(uOffset % 512 == 0);
    Assert(cbToRead % 512 == 0);

    pExtent = vciCacheExtentLookup(pCache, offBlockAddr, NULL);
    if (pExtent)
    {
        uint64_t offRead = offBlockAddr - pExtent->u64BlockOffset;
        cBlocksToRead = RT_MIN(cBlocksToRead, pExtent->u32Blocks - offRead);

        rc = vdIfIoIntFileReadUser(pCache->pIfIo, pCache->pStorage,
                                   pExtent->u64BlockAddr + offRead,
                                   pIoCtx, cBlocksToRead);
    }
    else
    {
        /** @todo Best fit to check whether we have cached data later and set
         * pcbActuallyRead accordingly. */
        rc = VERR_VD_BLOCK_FREE;
    }

    if (pcbActuallyRead)
        *pcbActuallyRead = VCI_BLOCK2BYTE(cBlocksToRead);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnWrite */
static DECLCALLBACK(int) vciWrite(void *pBackendData, uint64_t uOffset, size_t cbToWrite,
                                  PVDIOCTX pIoCtx, size_t *pcbWriteProcess)
{
    RT_NOREF5(pBackendData, uOffset, cbToWrite, pIoCtx, pcbWriteProcess);
    LogFlowFunc(("pBackendData=%#p uOffset=%llu cbToWrite=%zu pIoCtx=%#p pcbWriteProcess=%#p\n",
                 pBackendData, uOffset, cbToWrite, pIoCtx, pcbWriteProcess));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc = VINF_SUCCESS;
    uint64_t cBlocksToWrite = VCI_BYTE2BLOCK(cbToWrite);
    //uint64_t offBlockAddr  = VCI_BYTE2BLOCK(uOffset);

    AssertPtr(pCache); NOREF(pCache);
    Assert(uOffset % 512 == 0);
    Assert(cbToWrite % 512 == 0);
    while (cBlocksToWrite)
    {

    }

    *pcbWriteProcess = cbToWrite; /** @todo Implement. */

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnFlush */
static DECLCALLBACK(int) vciFlush(void *pBackendData, PVDIOCTX pIoCtx)
{
    RT_NOREF1(pIoCtx);
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVCICACHE pCache = (PVCICACHE)pBackendData;

    int rc = vciFlushImage(pCache);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnGetVersion */
static DECLCALLBACK(unsigned) vciGetVersion(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVCICACHE pCache = (PVCICACHE)pBackendData;

    AssertPtr(pCache);

    if (pCache)
        return 1;
    else
        return 0;
}

/** @copydoc VDCACHEBACKEND::pfnGetSize */
static DECLCALLBACK(uint64_t) vciGetSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    uint64_t cb = 0;

    AssertPtr(pCache);

    if (pCache && pCache->pStorage)
        cb = pCache->cbSize;

    LogFlowFunc(("returns %llu\n", cb));
    return cb;
}

/** @copydoc VDCACHEBACKEND::pfnGetFileSize */
static DECLCALLBACK(uint64_t) vciGetFileSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    uint64_t cb = 0;

    AssertPtr(pCache);

    if (pCache)
    {
        uint64_t cbFile;
        if (pCache->pStorage)
        {
            int rc = vdIfIoIntFileGetSize(pCache->pIfIo, pCache->pStorage, &cbFile);
            if (RT_SUCCESS(rc))
                cb = cbFile;
        }
    }

    LogFlowFunc(("returns %lld\n", cb));
    return cb;
}

/** @copydoc VDCACHEBACKEND::pfnGetImageFlags */
static DECLCALLBACK(unsigned) vciGetImageFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    unsigned uImageFlags;

    AssertPtr(pCache);

    if (pCache)
        uImageFlags = pCache->uImageFlags;
    else
        uImageFlags = 0;

    LogFlowFunc(("returns %#x\n", uImageFlags));
    return uImageFlags;
}

/** @copydoc VDCACHEBACKEND::pfnGetOpenFlags */
static DECLCALLBACK(unsigned) vciGetOpenFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    unsigned uOpenFlags;

    AssertPtr(pCache);

    if (pCache)
        uOpenFlags = pCache->uOpenFlags;
    else
        uOpenFlags = 0;

    LogFlowFunc(("returns %#x\n", uOpenFlags));
    return uOpenFlags;
}

/** @copydoc VDCACHEBACKEND::pfnSetOpenFlags */
static DECLCALLBACK(int) vciSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    LogFlowFunc(("pBackendData=%#p\n uOpenFlags=%#x", pBackendData, uOpenFlags));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc;

    /* Image must be opened and the new flags must be valid. Just readonly and
     * info flags are supported. */
    if (!pCache || (uOpenFlags & ~(VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO)))
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Implement this operation via reopening the image. */
    rc = vciFreeImage(pCache, false);
    if (RT_FAILURE(rc))
        goto out;
    rc = vciOpenImage(pCache, uOpenFlags);

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnGetComment */
static DECLCALLBACK(int) vciGetComment(void *pBackendData, char *pszComment,
                                       size_t cbComment)
{
    RT_NOREF2(pszComment, cbComment);
    LogFlowFunc(("pBackendData=%#p pszComment=%#p cbComment=%zu\n", pBackendData, pszComment, cbComment));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc;

    AssertPtr(pCache);

    if (pCache)
        rc = VERR_NOT_SUPPORTED;
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc comment='%s'\n", rc, pszComment));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnSetComment */
static DECLCALLBACK(int) vciSetComment(void *pBackendData, const char *pszComment)
{
    RT_NOREF1(pszComment);
    LogFlowFunc(("pBackendData=%#p pszComment=\"%s\"\n", pBackendData, pszComment));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc;

    AssertPtr(pCache);

    if (pCache)
    {
        if (pCache->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            rc = VERR_VD_IMAGE_READ_ONLY;
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnGetUuid */
static DECLCALLBACK(int) vciGetUuid(void *pBackendData, PRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc;

    AssertPtr(pCache);

    if (pCache)
        rc = VERR_NOT_SUPPORTED;
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnSetUuid */
static DECLCALLBACK(int) vciSetUuid(void *pBackendData, PCRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc;

    LogFlowFunc(("%RTuuid\n", pUuid));
    AssertPtr(pCache);

    if (pCache)
    {
        if (!(pCache->uOpenFlags & VD_OPEN_FLAGS_READONLY))
            rc = VERR_NOT_SUPPORTED;
        else
            rc = VERR_VD_IMAGE_READ_ONLY;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnGetModificationUuid */
static DECLCALLBACK(int) vciGetModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc;

    AssertPtr(pCache);

    if (pCache)
        rc = VERR_NOT_SUPPORTED;
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnSetModificationUuid */
static DECLCALLBACK(int) vciSetModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVCICACHE pCache = (PVCICACHE)pBackendData;
    int rc;

    AssertPtr(pCache);

    if (pCache)
    {
        if (!(pCache->uOpenFlags & VD_OPEN_FLAGS_READONLY))
            rc = VERR_NOT_SUPPORTED;
        else
            rc = VERR_VD_IMAGE_READ_ONLY;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDCACHEBACKEND::pfnDump */
static DECLCALLBACK(void) vciDump(void *pBackendData)
{
    NOREF(pBackendData);
}


const VDCACHEBACKEND g_VciCacheBackend =
{
    /* u32Version */
    VD_CACHEBACKEND_VERSION,
    /* pszBackendName */
    "vci",
    /* uBackendCaps */
    VD_CAP_CREATE_FIXED | VD_CAP_CREATE_DYNAMIC | VD_CAP_FILE | VD_CAP_VFS,
    /* papszFileExtensions */
    s_apszVciFileExtensions,
    /* paConfigInfo */
    NULL,
    /* pfnProbe */
    vciProbe,
    /* pfnOpen */
    vciOpen,
    /* pfnCreate */
    vciCreate,
    /* pfnClose */
    vciClose,
    /* pfnRead */
    vciRead,
    /* pfnWrite */
    vciWrite,
    /* pfnFlush */
    vciFlush,
    /* pfnDiscard */
    NULL,
    /* pfnGetVersion */
    vciGetVersion,
    /* pfnGetSize */
    vciGetSize,
    /* pfnGetFileSize */
    vciGetFileSize,
    /* pfnGetImageFlags */
    vciGetImageFlags,
    /* pfnGetOpenFlags */
    vciGetOpenFlags,
    /* pfnSetOpenFlags */
    vciSetOpenFlags,
    /* pfnGetComment */
    vciGetComment,
    /* pfnSetComment */
    vciSetComment,
    /* pfnGetUuid */
    vciGetUuid,
    /* pfnSetUuid */
    vciSetUuid,
    /* pfnGetModificationUuid */
    vciGetModificationUuid,
    /* pfnSetModificationUuid */
    vciSetModificationUuid,
    /* pfnDump */
    vciDump,
    /* pfnComposeLocation */
    NULL,
    /* pfnComposeName */
    NULL,
    /* u32VersionEnd */
    VD_CACHEBACKEND_VERSION
};

