/* $Id: xfsvfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - XFS Virtual Filesystem.
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
#define LOG_GROUP RTLOGGROUP_FS
#include <iprt/fsvfs.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/formats/xfs.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The maximum allocation group cache size (in bytes). */
#if ARCH_BITS >= 64
# define RTFSXFS_MAX_AG_CACHE_SIZE          _512K
#else
# define RTFSXFS_MAX_AG_CACHE_SIZE          _128K
#endif
/** The maximum inode cache size (in bytes). */
#if ARCH_BITS >= 64
# define RTFSXFS_MAX_INODE_CACHE_SIZE       _512K
#else
# define RTFSXFS_MAX_INODE_CACHE_SIZE       _128K
#endif
/** The maximum extent tree cache size (in bytes). */
#if ARCH_BITS >= 64
# define RTFSXFS_MAX_BLOCK_CACHE_SIZE       _512K
#else
# define RTFSXFS_MAX_BLOCK_CACHE_SIZE       _128K
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the XFS filesystem data. */
typedef struct RTFSXFSVOL *PRTFSXFSVOL;


/**
 * Cached allocation group descriptor data.
 */
typedef struct RTFSXFSAG
{
    /** AVL tree node, indexed by the allocation group number. */
    AVLU32NODECORE    Core;
    /** List node for the LRU list used for eviction. */
    RTLISTNODE        NdLru;
    /** Reference counter. */
    volatile uint32_t cRefs;
    /** @todo */
} RTFSXFSAG;
/** Pointer to allocation group descriptor data. */
typedef RTFSXFSAG *PRTFSXFSAG;


/**
 * In-memory inode.
 */
typedef struct RTFSXFSINODE
{
    /** AVL tree node, indexed by the inode number. */
    AVLU64NODECORE    Core;
    /** List node for the inode LRU list used for eviction. */
    RTLISTNODE        NdLru;
    /** Reference counter. */
    volatile uint32_t cRefs;
    /** Byte offset in the backing file where the inode is stored.. */
    uint64_t          offInode;
    /** Inode data. */
    RTFSOBJINFO       ObjInfo;
    /** Inode data fork format. */
    uint8_t           enmFormat;
    /** Inode flags. */
    uint16_t          fFlags;
    /** Inode version. */
    uint8_t           uVersion;
    /** Number of extents in the data fork for XFS_INODE_FORMAT_EXTENTS. */
    uint32_t          cExtentsData;
    /** Raw inode data. */
    uint8_t           abData[1];
} RTFSXFSINODE;
/** Pointer to an in-memory inode. */
typedef RTFSXFSINODE *PRTFSXFSINODE;


/**
 * Block cache entry.
 */
typedef struct RTFSXFSBLOCKENTRY
{
    /** AVL tree node, indexed by the filesystem block number. */
    AVLU64NODECORE    Core;
    /** List node for the inode LRU list used for eviction. */
    RTLISTNODE        NdLru;
    /** Reference counter. */
    volatile uint32_t cRefs;
    /** The block data. */
    uint8_t           abData[1];
} RTFSXFSBLOCKENTRY;
/** Pointer to a block cache entry. */
typedef RTFSXFSBLOCKENTRY *PRTFSXFSBLOCKENTRY;


/**
 * Open directory instance.
 */
typedef struct RTFSXFSDIR
{
    /** Volume this directory belongs to. */
    PRTFSXFSVOL         pVol;
    /** The underlying inode structure. */
    PRTFSXFSINODE       pInode;
    /** Set if we've reached the end of the directory enumeration. */
    bool                fNoMoreFiles;
    /** Current offset into the directory where the next entry should be read. */
    uint64_t            offEntry;
    /** Next entry index (for logging purposes). */
    uint32_t            idxEntry;
} RTFSXFSDIR;
/** Pointer to an open directory instance. */
typedef RTFSXFSDIR *PRTFSXFSDIR;


/**
 * Open file instance.
 */
typedef struct RTFSXFSFILE
{
    /** Volume this directory belongs to. */
    PRTFSXFSVOL         pVol;
    /** The underlying inode structure. */
    PRTFSXFSINODE       pInode;
    /** Current offset into the file for I/O. */
    RTFOFF              offFile;
} RTFSXFSFILE;
/** Pointer to an open file instance. */
typedef RTFSXFSFILE *PRTFSXFSFILE;


/**
 * XFS filesystem volume.
 */
typedef struct RTFSXFSVOL
{
    /** Handle to itself. */
    RTVFS               hVfsSelf;
    /** The file, partition, or whatever backing the ext volume. */
    RTVFSFILE           hVfsBacking;
    /** The size of the backing thingy. */
    uint64_t            cbBacking;

    /** RTVFSMNT_F_XXX. */
    uint32_t            fMntFlags;
    /** RTFSXFSVFS_F_XXX (currently none defined). */
    uint32_t            fXfsFlags;

    /** Size of one sector. */
    size_t              cbSector;
    /** Size of one block. */
    size_t              cbBlock;
    /** Number of bits to shift for converting a block number to byte offset. */
    uint32_t            cBlockShift;
    /** Number of blocks per allocation group. */
    XFSAGNUMBER         cBlocksPerAg;
    /** Number of blocks per allocation group as log2. */
    uint32_t            cAgBlocksLog;
    /** Number of allocation groups for this volume. */
    uint32_t            cAgs;
    /** inode of the root directory. */
    XFSINO              uInodeRoot;
    /** Inode size in bytes. */
    size_t              cbInode;
    /** Number of inodes per block. */
    uint32_t            cInodesPerBlock;
    /** Number of inodes per block as log2. */
    uint32_t            cInodesPerBlockLog;

    /** @name Allocation group cache.
     * @{ */
    /** LRU list anchor. */
    RTLISTANCHOR        LstAgLru;
    /** Root of the cached allocation group tree. */
    AVLU32TREE          AgRoot;
    /** Size of the cached allocation groups. */
    size_t              cbAgs;
    /** @} */

    /** @name Inode cache.
     * @{ */
    /** LRU list anchor for the inode cache. */
    RTLISTANCHOR        LstInodeLru;
    /** Root of the cached inode tree. */
    AVLU64TREE          InodeRoot;
    /** Size of the cached inodes. */
    size_t              cbInodes;
    /** @} */

    /** @name Block cache.
     * @{ */
    /** LRU list anchor for the block cache. */
    RTLISTANCHOR        LstBlockLru;
    /** Root of the cached block tree. */
    AVLU64TREE          BlockRoot;
    /** Size of cached blocks. */
    size_t              cbBlocks;
    /** @} */
} RTFSXFSVOL;



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtFsXfsVol_OpenDirByInode(PRTFSXFSVOL pThis, uint32_t iInode, PRTVFSDIR phVfsDir);

#ifdef LOG_ENABLED
/**
 * Logs the XFS filesystem superblock.
 *
 * @param   iAg                 The allocation group number for the given super block.
 * @param   pSb                 Pointer to the superblock.
 */
static void rtFsXfsSb_Log(uint32_t iAg, PCXFSSUPERBLOCK pSb)
{
    if (LogIs2Enabled())
    {
        Log2(("XFS: Superblock %#RX32:\n", iAg));
        Log2(("XFS:   u32Magic                    %#RX32\n", RT_BE2H_U32(pSb->u32Magic)));
        Log2(("XFS:   cbBlock                     %RU32\n", RT_BE2H_U32(pSb->cbBlock)));
        Log2(("XFS:   cBlocks                     %RU64\n", RT_BE2H_U64(pSb->cBlocks)));
        Log2(("XFS:   cBlocksRtDev                %RU64\n", RT_BE2H_U64(pSb->cBlocksRtDev)));
        Log2(("XFS:   cExtentsRtDev               %RU64\n", RT_BE2H_U64(pSb->cExtentsRtDev)));
        Log2(("XFS:   abUuid                      <todo>\n"));
        Log2(("XFS:   uBlockJournal               %#RX64\n", RT_BE2H_U64(pSb->uBlockJournal)));
        Log2(("XFS:   uInodeRoot                  %#RX64\n", RT_BE2H_U64(pSb->uInodeRoot)));
        Log2(("XFS:   uInodeBitmapRtExt           %#RX64\n", RT_BE2H_U64(pSb->uInodeBitmapRtExt)));
        Log2(("XFS:   uInodeBitmapSummary         %#RX64\n", RT_BE2H_U64(pSb->uInodeBitmapSummary)));
        Log2(("XFS:   cRtExtent                   %RU32\n", RT_BE2H_U32(pSb->cRtExtent)));
        Log2(("XFS:   cAgBlocks                   %RU32\n", RT_BE2H_U32(pSb->cAgBlocks)));
        Log2(("XFS:   cAg                         %RU32\n", RT_BE2H_U32(pSb->cAg)));
        Log2(("XFS:   cRtBitmapBlocks             %RU32\n", RT_BE2H_U32(pSb->cRtBitmapBlocks)));
        Log2(("XFS:   cJournalBlocks              %RU32\n", RT_BE2H_U32(pSb->cJournalBlocks)));
        Log2(("XFS:   fVersion                    %#RX16%s%s%s%s%s%s%s%s%s%s%s\n", RT_BE2H_U16(pSb->fVersion),
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_ATTR   ? " attr"   : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_NLINK  ? " nlink"  : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_QUOTA  ? " quota"  : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_ALIGN  ? " align"  : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_DALIGN ? " dalign" : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_SHARED ? " shared" : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_LOGV2  ? " logv2"  : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_SECTOR ? " sector" : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_EXTFLG ? " extflg" : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_DIRV2  ? " dirv2"  : "",
              RT_BE2H_U16(pSb->fVersion) & XFS_SB_VERSION_F_FEAT2  ? " feat2"  : ""));
        Log2(("XFS:   cbSector                    %RU16\n", RT_BE2H_U16(pSb->cbSector)));
        Log2(("XFS:   cbInode                     %RU16\n", RT_BE2H_U16(pSb->cbInode)));
        Log2(("XFS:   cIndoesPerBlock             %RU16\n", RT_BE2H_U16(pSb->cInodesPerBlock)));
        Log2(("XFS:   achFsName                   %12s\n", &pSb->achFsName[0]));
        Log2(("XFS:   cBlockSzLog                 %RU8\n", pSb->cBlockSzLog));
        Log2(("XFS:   cSectorSzLog                %RU8\n", pSb->cSectorSzLog));
        Log2(("XFS:   cInodeSzLog                 %RU8\n", pSb->cInodeSzLog));
        Log2(("XFS:   cInodesPerBlockLog          %RU8\n", pSb->cInodesPerBlockLog));
        Log2(("XFS:   cAgBlocksLog                %RU8\n", pSb->cAgBlocksLog));
        Log2(("XFS:   cExtentsRtDevLog            %RU8\n", pSb->cExtentsRtDevLog));
        Log2(("XFS:   fInProgress                 %RU8\n", pSb->fInProgress));
        Log2(("XFS:   cInodeMaxPct                %RU8\n", pSb->cInodeMaxPct));
        Log2(("XFS:   cInodesGlobal               %#RX64\n", RT_BE2H_U64(pSb->cInodesGlobal)));
        Log2(("XFS:   cInodesGlobalFree           %#RX64\n", RT_BE2H_U64(pSb->cInodesGlobalFree)));
        Log2(("XFS:   cBlocksFree                 %#RX64\n", RT_BE2H_U64(pSb->cBlocksFree)));
        Log2(("XFS:   cExtentsRtFree              %#RX64\n", RT_BE2H_U64(pSb->cExtentsRtFree)));
        Log2(("XFS:   uInodeQuotaUsr              %#RX64\n", RT_BE2H_U64(pSb->uInodeQuotaUsr)));
        Log2(("XFS:   uInodeQuotaGrp              %#RX64\n", RT_BE2H_U64(pSb->uInodeQuotaGrp)));
        Log2(("XFS:   fQuotaFlags                 %#RX16\n", RT_BE2H_U16(pSb->fQuotaFlags)));
        Log2(("XFS:   fFlagsMisc                  %#RX8\n", pSb->fFlagsMisc));
        Log2(("XFS:   uSharedVn                   %#RX8\n", pSb->uSharedVn));
        Log2(("XFS:   cBlocksInodeAlignment       %#RX32\n", RT_BE2H_U32(pSb->cBlocksInodeAlignment)));
        Log2(("XFS:   cBlocksRaidStripe           %#RX32\n", RT_BE2H_U32(pSb->cBlocksRaidStripe)));
        Log2(("XFS:   cBlocksRaidWidth            %#RX32\n", RT_BE2H_U32(pSb->cBlocksRaidWidth)));
        Log2(("XFS:   cDirBlockAllocLog           %RU8\n", pSb->cDirBlockAllocLog));
        Log2(("XFS:   cLogDevSubVolSectorSzLog    %RU8\n", pSb->cLogDevSubVolSectorSzLog));
        Log2(("XFS:   cLogDevSectorSzLog          %RU16\n", RT_BE2H_U16(pSb->cLogDevSectorSzLog)));
        Log2(("XFS:   cLogDevRaidStripe           %RU32\n", RT_BE2H_U32(pSb->cLogDevRaidStripe)));
        Log2(("XFS:   fFeatures2                  %#RX32\n", RT_BE2H_U32(pSb->fFeatures2)));
        Log2(("XFS:   fFeaturesRw                 %#RX32\n", RT_BE2H_U32(pSb->fFeaturesRw)));
        Log2(("XFS:   fFeaturesRo                 %#RX32\n", RT_BE2H_U32(pSb->fFeaturesRo)));
        Log2(("XFS:   fFeaturesIncompatRw         %#RX32\n", RT_BE2H_U32(pSb->fFeaturesIncompatRw)));
        Log2(("XFS:   fFeaturesJrnlIncompatRw     %#RX32\n", RT_BE2H_U32(pSb->fFeaturesJrnlIncompatRw)));
        Log2(("XFS:   u32Chksum                   %#RX32\n", RT_BE2H_U32(pSb->u32Chksum)));
        Log2(("XFS:   u32SparseInodeAlignment     %#RX32\n", RT_BE2H_U32(pSb->u32SparseInodeAlignment)));
        Log2(("XFS:   uInodeProjectQuota          %#RX64\n", RT_BE2H_U64(pSb->uInodeProjectQuota)));
        Log2(("XFS:   uJrnlSeqSbUpdate            %#RX64\n", RT_BE2H_U64(pSb->uJrnlSeqSbUpdate)));
        Log2(("XFS:   abUuidMeta                  <todo>\n"));
        Log2(("XFS:   uInodeRm                    %#RX64\n", RT_BE2H_U64(pSb->uInodeRm)));
    }
}

#if 0 /* unused */
/**
 * Logs a AG free space block.
 *
 * @param   iAg                 The allocation group number for the given free space block.
 * @param   pAgf                The AG free space block.
 */
static void rtFsXfsAgf_Log(uint32_t iAg, PCXFSAGF pAgf)
{
    if (LogIs2Enabled())
    {
        Log2(("XFS: AGF %#RX32:\n", iAg));
        Log2(("XFS:   u32Magic                    %#RX32\n", RT_BE2H_U32(pAgf->u32Magic)));
        Log2(("XFS:   uVersion                    %#RX32\n", RT_BE2H_U32(pAgf->uVersion)));
        Log2(("XFS:   uSeqNo                      %#RX32\n", RT_BE2H_U32(pAgf->uSeqNo)));
        Log2(("XFS:   cLengthBlocks               %#RX32\n", RT_BE2H_U32(pAgf->cLengthBlocks)));
        Log2(("XFS:   auRoots[0]                  %#RX32\n", RT_BE2H_U32(pAgf->auRoots[0])));
        Log2(("XFS:   auRoots[1]                  %#RX32\n", RT_BE2H_U32(pAgf->auRoots[1])));
        Log2(("XFS:   auRoots[2]                  %#RX32\n", RT_BE2H_U32(pAgf->auRoots[2])));
        Log2(("XFS:   acLvls[0]                   %RU32\n", RT_BE2H_U32(pAgf->acLvls[0])));
        Log2(("XFS:   acLvls[1]                   %RU32\n", RT_BE2H_U32(pAgf->acLvls[1])));
        Log2(("XFS:   acLvls[2]                   %RU32\n", RT_BE2H_U32(pAgf->acLvls[2])));
        Log2(("XFS:   idxFreeListFirst            %RU32\n", RT_BE2H_U32(pAgf->idxFreeListFirst)));
        Log2(("XFS:   idxFreeListLast             %RU32\n", RT_BE2H_U32(pAgf->idxFreeListLast)));
        Log2(("XFS:   cFreeListBlocks             %RU32\n", RT_BE2H_U32(pAgf->cFreeListBlocks)));
        Log2(("XFS:   cFreeBlocks                 %RU32\n", RT_BE2H_U32(pAgf->cFreeBlocks)));
        Log2(("XFS:   cFreeBlocksLongest          %RU32\n", RT_BE2H_U32(pAgf->cFreeBlocksLongest)));
        Log2(("XFS:   cBlocksBTrees               %RU32\n", RT_BE2H_U32(pAgf->cBlocksBTrees)));
        Log2(("XFS:   abUuid                      <todo>\n"));
        Log2(("XFS:   cBlocksRevMap               %RU32\n", RT_BE2H_U32(pAgf->cBlocksRevMap)));
        Log2(("XFS:   cBlocksRefcountBTree        %RU32\n", RT_BE2H_U32(pAgf->cBlocksRefcountBTree)));
        Log2(("XFS:   uRootRefcount               %#RX32\n", RT_BE2H_U32(pAgf->uRootRefcount)));
        Log2(("XFS:   cLvlRefcount                %RU32\n", RT_BE2H_U32(pAgf->cLvlRefcount)));
        Log2(("XFS:   uSeqNoLastWrite             %#RX64\n", RT_BE2H_U64(pAgf->uSeqNoLastWrite)));
        Log2(("XFS:   uChkSum                     %#RX32\n", RT_BE2H_U32(pAgf->uChkSum)));
    }
}
#endif

/**
 * Loads an AG inode information block.
 *
 * @param   iAg                 The allocation group number for the given inode information block.
 * @param   pAgi                The AG inode information block.
 */
static void rtFsXfsAgi_Log(uint32_t iAg, PCXFSAGI pAgi)
{
    if (LogIs2Enabled())
    {
        Log2(("XFS: AGI %#RX32:\n", iAg));
        Log2(("XFS:   u32Magic                    %#RX32\n", RT_BE2H_U32(pAgi->u32Magic)));
        Log2(("XFS:   uVersion                    %#RX32\n", RT_BE2H_U32(pAgi->uVersion)));
        Log2(("XFS:   uSeqNo                      %#RX32\n", RT_BE2H_U32(pAgi->uSeqNo)));
        Log2(("XFS:   cLengthBlocks               %#RX32\n", RT_BE2H_U32(pAgi->cLengthBlocks)));
        Log2(("XFS:   cInodesAlloc                %#RX32\n", RT_BE2H_U32(pAgi->cInodesAlloc)));
        Log2(("XFS:   uRootInode                  %#RX32\n", RT_BE2H_U32(pAgi->uRootInode)));
        Log2(("XFS:   cLvlsInode                  %RU32\n", RT_BE2H_U32(pAgi->cLvlsInode)));
        Log2(("XFS:   uInodeNew                   %#RX32\n", RT_BE2H_U32(pAgi->uInodeNew)));
        Log2(("XFS:   uInodeDir                   %#RX32\n", RT_BE2H_U32(pAgi->uInodeDir)));
        Log2(("XFS:   au32HashUnlinked[0..63]     <todo>\n"));
        Log2(("XFS:   abUuid                      <todo>\n"));
        Log2(("XFS:   uChkSum                     %#RX32\n", RT_BE2H_U32(pAgi->uChkSum)));
        Log2(("XFS:   uSeqNoLastWrite             %#RX64\n", RT_BE2H_U64(pAgi->uSeqNoLastWrite)));
        Log2(("XFS:   uRootFreeInode              %#RX32\n", RT_BE2H_U32(pAgi->uRootFreeInode)));
        Log2(("XFS:   cLvlsFreeInode              %RU32\n", RT_BE2H_U32(pAgi->cLvlsFreeInode)));
    }
}


/**
 * Logs a XFS filesystem inode.
 *
 * @param   pThis               The XFS volume instance.
 * @param   iInode              Inode number.
 * @param   pInode              Pointer to the inode.
 */
static void rtFsXfsInode_Log(PRTFSXFSVOL pThis, XFSINO iInode, PCXFSINODECORE pInode)
{
    RT_NOREF(pThis);

    if (LogIs2Enabled())
    {
        RTTIMESPEC Spec;
        char       sz[80];

        Log2(("XFS: Inode %#RX64:\n", iInode));
        Log2(("XFS:   u16Magic                    %#RX16\n", RT_BE2H_U16(pInode->u16Magic)));
        Log2(("XFS:   fMode                       %#RX16\n", RT_BE2H_U16(pInode->fMode)));
        Log2(("XFS:   iVersion                    %#RX8\n", pInode->iVersion));
        Log2(("XFS:   enmFormat                   %#RX8\n", pInode->enmFormat));
        Log2(("XFS:   cOnLinks                    %RU16\n", RT_BE2H_U16(pInode->cOnLinks)));
        Log2(("XFS:   uUid                        %#RX32\n", RT_BE2H_U32(pInode->uUid)));
        Log2(("XFS:   uGid                        %#RX32\n", RT_BE2H_U32(pInode->uGid)));
        Log2(("XFS:   cLinks                      %#RX32\n", RT_BE2H_U32(pInode->cLinks)));
        Log2(("XFS:   uProjIdLow                  %#RX16\n", RT_BE2H_U16(pInode->uProjIdLow)));
        Log2(("XFS:   uProjIdHigh                 %#RX16\n", RT_BE2H_U16(pInode->uProjIdHigh)));
        Log2(("XFS:   cFlush                      %RU16\n", RT_BE2H_U16(pInode->cFlush)));
        Log2(("XFS:   TsLastAccessed              %#RX32:%#RX32 %s\n", RT_BE2H_U32(pInode->TsLastAccessed.cSecEpoch),
              RT_BE2H_U32(pInode->TsLastAccessed.cNanoSec),
              RTTimeSpecToString(RTTimeSpecAddNano(RTTimeSpecSetSeconds(&Spec, RT_BE2H_U32(pInode->TsLastAccessed.cSecEpoch)),
                                                   RT_BE2H_U32(pInode->TsLastAccessed.cNanoSec)),
                                 sz, sizeof(sz))));
        Log2(("XFS:   TsLastModified              %#RX32:%#RX32 %s\n", RT_BE2H_U32(pInode->TsLastModified.cSecEpoch),
              RT_BE2H_U32(pInode->TsLastModified.cNanoSec),
              RTTimeSpecToString(RTTimeSpecAddNano(RTTimeSpecSetSeconds(&Spec, RT_BE2H_U32(pInode->TsLastModified.cSecEpoch)),
                                                   RT_BE2H_U32(pInode->TsLastModified.cNanoSec)),
                                 sz, sizeof(sz))));
        Log2(("XFS:   TsCreatedModified           %#RX32:%#RX32 %s\n", RT_BE2H_U32(pInode->TsCreatedModified.cSecEpoch),
              RT_BE2H_U32(pInode->TsCreatedModified.cNanoSec),
              RTTimeSpecToString(RTTimeSpecAddNano(RTTimeSpecSetSeconds(&Spec, RT_BE2H_U32(pInode->TsCreatedModified.cSecEpoch)),
                                                   RT_BE2H_U32(pInode->TsCreatedModified.cNanoSec)),
                                 sz, sizeof(sz))));
        Log2(("XFS:   cbInode                     %#RX64\n", RT_BE2H_U64(pInode->cbInode)));
        Log2(("XFS:   cBlocks                     %#RX64\n", RT_BE2H_U64(pInode->cBlocks)));
        Log2(("XFS:   cExtentBlocksMin            %#RX32\n", RT_BE2H_U32(pInode->cExtentBlocksMin)));
        Log2(("XFS:   cExtentsData                %#RX32\n", RT_BE2H_U32(pInode->cExtentsData)));
        Log2(("XFS:   cExtentsAttr                %#RX16\n", RT_BE2H_U16(pInode->cExtentsAttr)));
        Log2(("XFS:   offAttrFork                 %#RX8\n", pInode->offAttrFork));
        Log2(("XFS:   enmFormatAttr               %#RX8\n", pInode->enmFormatAttr));
        Log2(("XFS:   fEvtMaskDmig                %#RX32\n", RT_BE2H_U32(pInode->fEvtMaskDmig)));
        Log2(("XFS:   uStateDmig                  %#RX16\n", RT_BE2H_U16(pInode->uStateDmig)));
        Log2(("XFS:   fFlags                      %#RX16\n", RT_BE2H_U16(pInode->fFlags)));
        Log2(("XFS:   cGeneration                 %#RX32\n", RT_BE2H_U32(pInode->cGeneration)));
        Log2(("XFS:   offBlockUnlinkedNext        %#RX32\n", RT_BE2H_U32(pInode->offBlockUnlinkedNext)));
        Log2(("XFS:   uChkSum                     %#RX32\n", RT_BE2H_U32(pInode->uChkSum)));
        Log2(("XFS:   cAttrChanges                %#RX64\n", RT_BE2H_U64(pInode->cAttrChanges)));
        Log2(("XFS:   uFlushSeqNo                 %#RX64\n", RT_BE2H_U64(pInode->uFlushSeqNo)));
        Log2(("XFS:   fFlags2                     %#RX64\n", RT_BE2H_U64(pInode->fFlags2)));
        Log2(("XFS:   cExtentCowMin               %#RX32\n", RT_BE2H_U32(pInode->cExtentCowMin)));
        Log2(("XFS:   TsCreation                  %#RX32:%#RX32 %s\n", RT_BE2H_U32(pInode->TsCreation.cSecEpoch),
              RT_BE2H_U32(pInode->TsCreation.cNanoSec),
              RTTimeSpecToString(RTTimeSpecAddNano(RTTimeSpecSetSeconds(&Spec, RT_BE2H_U32(pInode->TsCreation.cSecEpoch)),
                                                   RT_BE2H_U32(pInode->TsCreation.cNanoSec)),
                                 sz, sizeof(sz))));
        Log2(("XFS:   uInode                      %#RX64\n", RT_BE2H_U64(pInode->uInode)));
        Log2(("XFS:   abUuid                      <todo>\n"));
    }
}


#if 0
/**
 * Logs a XFS filesystem directory entry.
 *
 * @param   pThis               The XFS volume instance.
 * @param   idxDirEntry         Directory entry index number.
 * @param   pDirEntry           The directory entry.
 */
static void rtFsXfsDirEntry_Log(PRTFSXFSVOL pThis, uint32_t idxDirEntry, PCXFSDIRENTRYEX pDirEntry)
{
    if (LogIs2Enabled())
    {
    }
}
#endif
#endif


/**
 * Converts a block number to a byte offset.
 *
 * @returns Offset in bytes for the given block number.
 * @param   pThis               The XFS volume instance.
 * @param   iBlock              The block number to convert.
 */
DECLINLINE(uint64_t) rtFsXfsBlockIdxToDiskOffset(PRTFSXFSVOL pThis, uint64_t iBlock)
{
    return iBlock << pThis->cBlockShift;
}


/**
 * Converts a byte offset to a block number.
 *
 * @returns Block number.
 * @param   pThis               The XFS volume instance.
 * @param   iBlock              The offset to convert.
 */
DECLINLINE(uint64_t) rtFsXfsDiskOffsetToBlockIdx(PRTFSXFSVOL pThis, uint64_t off)
{
    return off >> pThis->cBlockShift;
}


/**
 * Splits the given absolute inode number into the AG number, block inside the AG
 * and the offset into the block where to find the inode structure.
 *
 * @param   pThis               The XFS volume instance.
 * @param   iInode              The inode to split.
 * @param   piAg                Where to store the AG number.
 * @param   puBlock             Where to store the block number inside the AG.
 * @param   poffBlock           Where to store the offset into the block.
 */
DECLINLINE(void) rtFsXfsInodeSplitAbs(PRTFSXFSVOL pThis, XFSINO iInode,
                                      uint32_t *piAg, uint32_t *puBlock,
                                      uint32_t *poffBlock)
{
    *poffBlock = iInode & (pThis->cInodesPerBlock - 1);
    iInode >>= pThis->cInodesPerBlockLog;
    *puBlock = iInode & (RT_BIT_32(pThis->cAgBlocksLog) - 1); /* Using the log2 value here as it is rounded. */
    iInode >>= RT_BIT_32(pThis->cAgBlocksLog) - 1;
    *piAg = (uint32_t)iInode;
}


/**
 * Returns the size of the core inode structure on disk for the given version.
 *
 * @returns Size of the on disk inode structure in bytes.
 * @param   uVersion            The inode version.
 */
DECLINLINE(size_t) rtFsXfsInodeGetSz(uint8_t uVersion)
{
    if (uVersion < 3)
        return RT_OFFSETOF(XFSINODECORE, uChkSum);
    return sizeof(XFSINODECORE);
}


/**
 * Returns the pointer to the data fork of the given inode.
 *
 * @returns Pointer to the data fork.
 * @param   pThis               The XFS volume instance.
 * @param   pInode              The inode to get the data fork for.
 * @param   pcb                 Where to store the size of the remaining data area beginning with the fork.
 */
DECLINLINE(void *) rtFsXfsInodeGetDataFork(PRTFSXFSVOL pThis, PRTFSXFSINODE pInode, size_t *pcb)
{
    size_t offDataFork = rtFsXfsInodeGetSz(pInode->uVersion);
    size_t cbInodeData = pThis->cbInode - offDataFork;
    if (pcb)
        *pcb = cbInodeData;

    return &pInode->abData[offDataFork];
}


/**
 * Allocates a new block group.
 *
 * @returns Pointer to the new block group descriptor or NULL if out of memory.
 * @param   pThis               The XFS volume instance.
 * @param   cbAlloc             How much to allocate.
 * @param   iBlockGroup         Block group number.
 */
static PRTFSXFSBLOCKENTRY rtFsXfsVol_BlockAlloc(PRTFSXFSVOL pThis, size_t cbAlloc, uint64_t iBlock)
{
    PRTFSXFSBLOCKENTRY pBlock = (PRTFSXFSBLOCKENTRY)RTMemAllocZ(cbAlloc);
    if (RT_LIKELY(pBlock))
    {
        pBlock->Core.Key = iBlock;
        pBlock->cRefs    = 0;
        pThis->cbBlocks  += cbAlloc;
    }

    return pBlock;
}


/**
 * Returns a new block entry utilizing the cache if possible.
 *
 * @returns Pointer to the new block entry or NULL if out of memory.
 * @param   pThis               The XFS volume instance.
 * @param   iBlock              Block number.
 */
static PRTFSXFSBLOCKENTRY rtFsXfsVol_BlockGetNew(PRTFSXFSVOL pThis, uint64_t iBlock)
{
    PRTFSXFSBLOCKENTRY pBlock = NULL;
    size_t cbAlloc = RT_UOFFSETOF_DYN(RTFSXFSBLOCKENTRY, abData[pThis->cbBlock]);
    if (pThis->cbBlocks + cbAlloc <= RTFSXFS_MAX_BLOCK_CACHE_SIZE)
        pBlock = rtFsXfsVol_BlockAlloc(pThis, cbAlloc, iBlock);
    else
    {
        pBlock = RTListRemoveLast(&pThis->LstBlockLru, RTFSXFSBLOCKENTRY, NdLru);
        if (!pBlock)
            pBlock = rtFsXfsVol_BlockAlloc(pThis, cbAlloc, iBlock);
        else
        {
            /* Remove the block group from the tree because it gets a new key. */
            PAVLU64NODECORE pCore = RTAvlU64Remove(&pThis->BlockRoot, pBlock->Core.Key);
            Assert(pCore == &pBlock->Core); RT_NOREF(pCore);
        }
    }

    Assert(!pBlock->cRefs);
    pBlock->Core.Key = iBlock;
    pBlock->cRefs    = 1;

    return pBlock;
}


/**
 * Frees the given block.
 *
 * @param   pThis               The XFS volume instance.
 * @param   pBlock              The block to free.
 */
static void rtFsXfsVol_BlockFree(PRTFSXFSVOL pThis, PRTFSXFSBLOCKENTRY pBlock)
{
    Assert(!pBlock->cRefs);

    /*
     * Put it into the cache if the limit wasn't exceeded, otherwise the block group
     * is freed right away.
     */
    if (pThis->cbBlocks <= RTFSXFS_MAX_BLOCK_CACHE_SIZE)
    {
        /* Put onto the LRU list. */
        RTListPrepend(&pThis->LstBlockLru, &pBlock->NdLru);
    }
    else
    {
        /* Remove from the tree and free memory. */
        PAVLU64NODECORE pCore = RTAvlU64Remove(&pThis->BlockRoot, pBlock->Core.Key);
        Assert(pCore == &pBlock->Core); RT_NOREF(pCore);
        RTMemFree(pBlock);
        pThis->cbBlocks -= RT_UOFFSETOF_DYN(RTFSXFSBLOCKENTRY, abData[pThis->cbBlock]);
    }
}


/**
 * Gets the specified block data from the volume.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   iBlock              The filesystem block to load.
 * @param   ppBlock             Where to return the pointer to the block entry on success.
 * @param   ppvData             Where to return the pointer to the block data on success.
 */
static int rtFsXfsVol_BlockLoad(PRTFSXFSVOL pThis, uint64_t iBlock, PRTFSXFSBLOCKENTRY *ppBlock, void **ppvData)
{
    int rc = VINF_SUCCESS;

    /* Try to fetch the block group from the cache first. */
    PRTFSXFSBLOCKENTRY pBlock = (PRTFSXFSBLOCKENTRY)RTAvlU64Get(&pThis->BlockRoot, iBlock);
    if (!pBlock)
    {
        /* Slow path, load from disk. */
        pBlock = rtFsXfsVol_BlockGetNew(pThis, iBlock);
        if (RT_LIKELY(pBlock))
        {
            uint64_t offRead = rtFsXfsBlockIdxToDiskOffset(pThis, iBlock);
            rc = RTVfsFileReadAt(pThis->hVfsBacking, offRead, &pBlock->abData[0], pThis->cbBlock, NULL);
            if (RT_SUCCESS(rc))
            {
                bool fIns = RTAvlU64Insert(&pThis->BlockRoot, &pBlock->Core);
                Assert(fIns); RT_NOREF(fIns);
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        /* Remove from current LRU list position and add to the beginning. */
        uint32_t cRefs = ASMAtomicIncU32(&pBlock->cRefs);
        if (cRefs == 1) /* Blocks get removed from the LRU list if they are referenced. */
            RTListNodeRemove(&pBlock->NdLru);
    }

    if (RT_SUCCESS(rc))
    {
        *ppBlock = pBlock;
        *ppvData = &pBlock->abData[0];
    }
    else if (pBlock)
    {
        ASMAtomicDecU32(&pBlock->cRefs);
        rtFsXfsVol_BlockFree(pThis, pBlock); /* Free the block. */
    }

    return rc;
}


/**
 * Releases a reference of the given block.
 *
 * @param   pThis               The XFS volume instance.
 * @param   pBlock              The block to release.
 */
static void rtFsXfsVol_BlockRelease(PRTFSXFSVOL pThis, PRTFSXFSBLOCKENTRY pBlock)
{
    uint32_t cRefs = ASMAtomicDecU32(&pBlock->cRefs);
    if (!cRefs)
        rtFsXfsVol_BlockFree(pThis, pBlock);
}

#if 0 /* unused */
/**
 * Allocates a new alloction group.
 *
 * @returns Pointer to the new allocation group descriptor or NULL if out of memory.
 * @param   pThis               The XFS volume instance.
 * @param   iAG                 Allocation group number.
 */
static PRTFSXFSAG rtFsXfsAg_Alloc(PRTFSXFSVOL pThis, uint32_t iAg)
{
    PRTFSXFSAG pAg = (PRTFSXFSAG)RTMemAllocZ(sizeof(RTFSXFSAG));
    if (RT_LIKELY(pAg))
    {
        pAg->Core.Key       = iAg;
        pAg->cRefs          = 0;
        pThis->cbAgs       += sizeof(RTFSXFSAG);
    }

    return pAg;
}


/**
 * Frees the given allocation group.
 *
 * @param   pThis               The XFS volume instance.
 * @param   pAg                 The allocation group to free.
 */
static void rtFsXfsAg_Free(PRTFSXFSVOL pThis, PRTFSXFSAG pAg)
{
    Assert(!pAg->cRefs);

    /*
     * Put it into the cache if the limit wasn't exceeded, otherwise the allocation group
     * is freed right away.
     */
    if (pThis->cbAgs <= RTFSXFS_MAX_AG_CACHE_SIZE)
    {
        /* Put onto the LRU list. */
        RTListPrepend(&pThis->LstAgLru, &pAg->NdLru);
    }
    else
    {
        /* Remove from the tree and free memory. */
        PAVLU32NODECORE pCore = RTAvlU32Remove(&pThis->AgRoot, pAg->Core.Key);
        Assert(pCore == &pAg->Core); RT_NOREF(pCore);
        RTMemFree(pAg);
        pThis->cbAgs -= sizeof(RTFSXFSAG);
    }
}


/**
 * Returns a new block group utilizing the cache if possible.
 *
 * @returns Pointer to the new block group descriptor or NULL if out of memory.
 * @param   pThis               The XFS volume instance.
 * @param   iAg                 Allocation group number.
 */
static PRTFSXFSAG rtFsXfsAg_GetNew(PRTFSXFSVOL pThis, uint32_t iAg)
{
    PRTFSXFSAG pAg = NULL;
    if (pThis->cbAgs + sizeof(RTFSXFSAG) <= RTFSXFS_MAX_AG_CACHE_SIZE)
        pAg = rtFsXfsAg_Alloc(pThis, iAg);
    else
    {
        pAg = RTListRemoveLast(&pThis->LstAgLru, RTFSXFSAG, NdLru);
        if (!pAg)
            pAg = rtFsXfsAg_Alloc(pThis, iAg);
        else
        {
            /* Remove the block group from the tree because it gets a new key. */
            PAVLU32NODECORE pCore = RTAvlU32Remove(&pThis->AgRoot, pAg->Core.Key);
            Assert(pCore == &pAg->Core); RT_NOREF(pCore);
        }
    }

    Assert(!pAg->cRefs);
    pAg->Core.Key = iAg;
    pAg->cRefs    = 1;

    return pAg;
}


/**
 * Loads the given allocation group number and returns it on success.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   iAg                 The allocation group to load.
 * @param   ppAg                Where to store the allocation group on success.
 */
static int rtFsXfsAg_Load(PRTFSXFSVOL pThis, uint32_t iAg, PRTFSXFSAG *ppAg)
{
    int rc = VINF_SUCCESS;

    AssertReturn(iAg < pThis->cAgs, VERR_VFS_BOGUS_FORMAT);

    /* Try to fetch the allocation group from the cache first. */
    PRTFSXFSAG pAg = (PRTFSXFSAG)RTAvlU32Get(&pThis->AgRoot, iAg);
    if (!pAg)
    {
        /* Slow path, load from disk. */
        pAg = rtFsXfsAg_GetNew(pThis, iAg);
        if (RT_LIKELY(pAg))
        {
            uint64_t offRead = rtFsXfsBlockIdxToDiskOffset(pThis, iAg * pThis->cBlocksPerAg);
            XFSSUPERBLOCK Sb;
            rc = RTVfsFileReadAt(pThis->hVfsBacking, offRead, &Sb, sizeof(Sb), NULL);
            if (RT_SUCCESS(rc))
            {
#ifdef LOG_ENABLED
                rtFsXfsSb_Log(iAg, &Sb);
#endif
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        /* Remove from current LRU list position and add to the beginning. */
        uint32_t cRefs = ASMAtomicIncU32(&pAg->cRefs);
        if (cRefs == 1) /* Block groups get removed from the LRU list if they are referenced. */
            RTListNodeRemove(&pAg->NdLru);
    }

    if (RT_SUCCESS(rc))
        *ppAg = pAg;
    else if (pAg)
    {
        ASMAtomicDecU32(&pAg->cRefs);
        rtFsXfsAg_Free(pThis, pAg); /* Free the allocation group. */
    }

    return rc;
}


/**
 * Releases a reference of the given allocation group.
 *
 * @param   pThis               The XFS volume instance.
 * @param   pAg                 The allocation group to release.
 */
static void rtFsXfsAg_Release(PRTFSXFSVOL pThis, PRTFSXFSAG pAg)
{
    uint32_t cRefs = ASMAtomicDecU32(&pAg->cRefs);
    if (!cRefs)
        rtFsXfsAg_Free(pThis, pAg);
}
#endif

/**
 * Allocates a new inode.
 *
 * @returns Pointer to the new inode or NULL if out of memory.
 * @param   pThis               The XFS volume instance.
 * @param   iInode              Inode number.
 */
static PRTFSXFSINODE rtFsXfsInode_Alloc(PRTFSXFSVOL pThis, uint32_t iInode)
{
    size_t cbAlloc = RT_UOFFSETOF_DYN(RTFSXFSINODE, abData[pThis->cbInode]);
    PRTFSXFSINODE pInode = (PRTFSXFSINODE)RTMemAllocZ(cbAlloc);
    if (RT_LIKELY(pInode))
    {
        pInode->Core.Key = iInode;
        pInode->cRefs    = 0;
        pThis->cbInodes  += cbAlloc;
    }

    return pInode;
}


/**
 * Frees the given inode.
 *
 * @param   pThis               The XFS volume instance.
 * @param   pInode              The inode to free.
 */
static void rtFsXfsInode_Free(PRTFSXFSVOL pThis, PRTFSXFSINODE pInode)
{
    Assert(!pInode->cRefs);

    /*
     * Put it into the cache if the limit wasn't exceeded, otherwise the inode
     * is freed right away.
     */
    if (pThis->cbInodes <= RTFSXFS_MAX_INODE_CACHE_SIZE)
    {
        /* Put onto the LRU list. */
        RTListPrepend(&pThis->LstInodeLru, &pInode->NdLru);
    }
    else
    {
        /* Remove from the tree and free memory. */
        PAVLU64NODECORE pCore = RTAvlU64Remove(&pThis->InodeRoot, pInode->Core.Key);
        Assert(pCore == &pInode->Core); RT_NOREF(pCore);
        RTMemFree(pInode);
        pThis->cbInodes -= RT_UOFFSETOF_DYN(RTFSXFSINODE, abData[pThis->cbInode]);
    }
}


/**
 * Returns a new inodep utilizing the cache if possible.
 *
 * @returns Pointer to the new inode or NULL if out of memory.
 * @param   pThis               The XFS volume instance.
 * @param   iInode              Inode number.
 */
static PRTFSXFSINODE rtFsXfsInode_GetNew(PRTFSXFSVOL pThis, XFSINO iInode)
{
    PRTFSXFSINODE pInode = NULL;
    if (pThis->cbInodes + RT_UOFFSETOF_DYN(RTFSXFSINODE, abData[pThis->cbInode]) <= RTFSXFS_MAX_INODE_CACHE_SIZE)
        pInode = rtFsXfsInode_Alloc(pThis, iInode);
    else
    {
        pInode = RTListRemoveLast(&pThis->LstInodeLru, RTFSXFSINODE, NdLru);
        if (!pInode)
            pInode = rtFsXfsInode_Alloc(pThis, iInode);
        else
        {
            /* Remove the block group from the tree because it gets a new key. */
            PAVLU64NODECORE pCore = RTAvlU64Remove(&pThis->InodeRoot, pInode->Core.Key);
            Assert(pCore == &pInode->Core); RT_NOREF(pCore);
        }
    }

    Assert(!pInode->cRefs);
    pInode->Core.Key = iInode;
    pInode->cRefs    = 1;

    return pInode;
}


/**
 * Loads the given inode number and returns it on success.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   iInode              The inode to load.
 * @param   ppInode             Where to store the inode on success.
 */
static int rtFsXfsInode_Load(PRTFSXFSVOL pThis, XFSINO iInode, PRTFSXFSINODE *ppInode)
{
    int rc = VINF_SUCCESS;

    /* Try to fetch the inode from the cache first. */
    PRTFSXFSINODE pInode = (PRTFSXFSINODE)RTAvlU64Get(&pThis->InodeRoot, iInode);
    if (!pInode)
    {
        /* Slow path, load from disk. */
        pInode = rtFsXfsInode_GetNew(pThis, iInode);
        if (RT_LIKELY(pInode))
        {
            uint32_t iAg;
            uint32_t uBlock;
            uint32_t offBlock;

            rtFsXfsInodeSplitAbs(pThis, iInode, &iAg, &uBlock, &offBlock);

            uint64_t offRead = (iAg * pThis->cBlocksPerAg + uBlock) * pThis->cbBlock + offBlock;
            rc = RTVfsFileReadAt(pThis->hVfsBacking, offRead, &pInode->abData[0], pThis->cbInode, NULL);
            if (RT_SUCCESS(rc))
            {
                PCXFSINODECORE pInodeCore = (PCXFSINODECORE)&pInode->abData[0];

#ifdef LOG_ENABLED
                rtFsXfsInode_Log(pThis, iInode, pInodeCore);
#endif

                pInode->offInode            = offRead;
                pInode->fFlags              = RT_BE2H_U16(pInodeCore->fFlags);
                pInode->enmFormat           = pInodeCore->enmFormat;
                pInode->cExtentsData        = RT_BE2H_U32(pInodeCore->cExtentsData);
                pInode->ObjInfo.cbObject    = RT_BE2H_U64(pInodeCore->cbInode);
                pInode->ObjInfo.cbAllocated = RT_BE2H_U64(pInodeCore->cBlocks) * pThis->cbBlock;
                RTTimeSpecSetSeconds(&pInode->ObjInfo.AccessTime, RT_BE2H_U32(pInodeCore->TsLastAccessed.cSecEpoch));
                RTTimeSpecAddNano(&pInode->ObjInfo.AccessTime, RT_BE2H_U32(pInodeCore->TsLastAccessed.cNanoSec));
                RTTimeSpecSetSeconds(&pInode->ObjInfo.ModificationTime, RT_BE2H_U32(pInodeCore->TsLastModified.cSecEpoch));
                RTTimeSpecAddNano(&pInode->ObjInfo.ModificationTime, RT_BE2H_U32(pInodeCore->TsLastModified.cNanoSec));
                RTTimeSpecSetSeconds(&pInode->ObjInfo.ChangeTime, RT_BE2H_U32(pInodeCore->TsCreatedModified.cSecEpoch));
                RTTimeSpecAddNano(&pInode->ObjInfo.ChangeTime, RT_BE2H_U32(pInodeCore->TsCreatedModified.cNanoSec));
                pInode->ObjInfo.Attr.enmAdditional = RTFSOBJATTRADD_UNIX;
                pInode->ObjInfo.Attr.u.Unix.uid    = RT_BE2H_U32(pInodeCore->uUid);
                pInode->ObjInfo.Attr.u.Unix.gid    = RT_BE2H_U32(pInodeCore->uGid);
                pInode->ObjInfo.Attr.u.Unix.cHardlinks = RT_BE2H_U16(pInodeCore->cOnLinks); /** @todo v2 inodes. */
                pInode->ObjInfo.Attr.u.Unix.INodeIdDevice = 0;
                pInode->ObjInfo.Attr.u.Unix.INodeId       = iInode;
                pInode->ObjInfo.Attr.u.Unix.fFlags        = 0;
                pInode->ObjInfo.Attr.u.Unix.GenerationId  = RT_BE2H_U32(pInodeCore->cGeneration);
                pInode->ObjInfo.Attr.u.Unix.Device        = 0;
                if (pInodeCore->iVersion >= 3)
                {
                    RTTimeSpecSetSeconds(&pInode->ObjInfo.BirthTime, RT_BE2H_U32(pInodeCore->TsCreation.cSecEpoch));
                    RTTimeSpecAddNano(&pInode->ObjInfo.BirthTime, RT_BE2H_U32(pInodeCore->TsCreation.cNanoSec));
                }
                else
                    pInode->ObjInfo.BirthTime = pInode->ObjInfo.ChangeTime;

                /* Fill in the mode. */
                pInode->ObjInfo.Attr.fMode = 0;
                uint16_t fInodeMode = RT_BE2H_U16(pInodeCore->fMode);
                switch (XFS_INODE_MODE_TYPE_GET_TYPE(fInodeMode))
                {
                    case XFS_INODE_MODE_TYPE_FIFO:
                        pInode->ObjInfo.Attr.fMode |= RTFS_TYPE_FIFO;
                        break;
                    case XFS_INODE_MODE_TYPE_CHAR:
                        pInode->ObjInfo.Attr.fMode |= RTFS_TYPE_DEV_CHAR;
                        break;
                    case XFS_INODE_MODE_TYPE_DIR:
                        pInode->ObjInfo.Attr.fMode |= RTFS_TYPE_DIRECTORY;
                        break;
                    case XFS_INODE_MODE_TYPE_BLOCK:
                        pInode->ObjInfo.Attr.fMode |= RTFS_TYPE_DEV_BLOCK;
                        break;
                    case XFS_INODE_MODE_TYPE_REGULAR:
                        pInode->ObjInfo.Attr.fMode |= RTFS_TYPE_FILE;
                        break;
                    case XFS_INODE_MODE_TYPE_SYMLINK:
                        pInode->ObjInfo.Attr.fMode |= RTFS_TYPE_SYMLINK;
                        break;
                    case XFS_INODE_MODE_TYPE_SOCKET:
                        pInode->ObjInfo.Attr.fMode |= RTFS_TYPE_SOCKET;
                        break;
                    default:
                        rc = VERR_VFS_BOGUS_FORMAT;
                }
                if (fInodeMode & XFS_INODE_MODE_EXEC_OTHER)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_IXOTH;
                if (fInodeMode & XFS_INODE_MODE_WRITE_OTHER)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_IWOTH;
                if (fInodeMode & XFS_INODE_MODE_READ_OTHER)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_IROTH;
                if (fInodeMode & XFS_INODE_MODE_EXEC_GROUP)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_IXGRP;
                if (fInodeMode & XFS_INODE_MODE_WRITE_GROUP)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_IWGRP;
                if (fInodeMode & XFS_INODE_MODE_READ_GROUP)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_IRGRP;
                if (fInodeMode & XFS_INODE_MODE_EXEC_OWNER)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_IXUSR;
                if (fInodeMode & XFS_INODE_MODE_WRITE_OWNER)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_IWUSR;
                if (fInodeMode & XFS_INODE_MODE_READ_OWNER)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_IRUSR;
                if (fInodeMode & XFS_INODE_MODE_STICKY)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_ISTXT;
                if (fInodeMode & XFS_INODE_MODE_SET_GROUP_ID)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_ISGID;
                if (fInodeMode & XFS_INODE_MODE_SET_USER_ID)
                    pInode->ObjInfo.Attr.fMode |= RTFS_UNIX_ISUID;
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        /* Remove from current LRU list position and add to the beginning. */
        uint32_t cRefs = ASMAtomicIncU32(&pInode->cRefs);
        if (cRefs == 1) /* Inodes get removed from the LRU list if they are referenced. */
            RTListNodeRemove(&pInode->NdLru);
    }

    if (RT_SUCCESS(rc))
        *ppInode = pInode;
    else if (pInode)
    {
        ASMAtomicDecU32(&pInode->cRefs);
        rtFsXfsInode_Free(pThis, pInode); /* Free the inode. */
    }

    return rc;
}


/**
 * Releases a reference of the given inode.
 *
 * @param   pThis               The XFS volume instance.
 * @param   pInode              The inode to release.
 */
static void rtFsXfsInode_Release(PRTFSXFSVOL pThis, PRTFSXFSINODE pInode)
{
    uint32_t cRefs = ASMAtomicDecU32(&pInode->cRefs);
    if (!cRefs)
        rtFsXfsInode_Free(pThis, pInode);
}


/**
 * Worker for various QueryInfo methods.
 *
 * @returns IPRT status code.
 * @param   pInode              The inode structure to return info for.
 * @param   pObjInfo            Where to return object info.
 * @param   enmAddAttr          What additional info to return.
 */
static int rtFsXfsInode_QueryInfo(PRTFSXFSINODE pInode, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RT_ZERO(*pObjInfo);

    pObjInfo->cbObject           = pInode->ObjInfo.cbObject;
    pObjInfo->cbAllocated        = pInode->ObjInfo.cbAllocated;
    pObjInfo->AccessTime         = pInode->ObjInfo.AccessTime;
    pObjInfo->ModificationTime   = pInode->ObjInfo.ModificationTime;
    pObjInfo->ChangeTime         = pInode->ObjInfo.ChangeTime;
    pObjInfo->BirthTime          = pInode->ObjInfo.BirthTime;
    pObjInfo->Attr.fMode         = pInode->ObjInfo.Attr.fMode;
    pObjInfo->Attr.enmAdditional = enmAddAttr;
    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_UNIX:
            memcpy(&pObjInfo->Attr.u.Unix, &pInode->ObjInfo.Attr.u.Unix, sizeof(pInode->ObjInfo.Attr.u.Unix));
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.u.UnixOwner.uid = pInode->ObjInfo.Attr.u.Unix.uid;
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.u.UnixGroup.gid = pInode->ObjInfo.Attr.u.Unix.gid;
            break;

        default:
            break;
    }

    return VINF_SUCCESS;
}


/**
 * Locates the location of the next level in the B+Tree mapping the given offset.
 *
 * @returns Filesystem block number where the next level of the B+Tree is stored.
 * @param   paoffFile           Array of file offset mappings.
 * @param   pauFsBlock          Array of filesystem block mappings.
 * @param   cEntries            Number of entries in the extent index node array.
 * @param   iBlock              The block to resolve.
 */
DECLINLINE(XFSDFSBNO) rtFsXfsInode_BTreeNdLocateNextLvl(XFSDFILOFF *paoffFile, XFSDFSBNO *pauFsBlock,
                                                        uint16_t cEntries, XFSDFILOFF offFile)
{
    for (uint32_t i = 1; i < cEntries; i++)
    {
        if (   RT_BE2H_U64(paoffFile[i - 1]) <= offFile
            && RT_BE2H_U64(paoffFile[i]) > offFile)
            return RT_BE2H_U64(pauFsBlock[i]);
    }

    /* Nothing found so far, the last entry must cover the block as the array is sorted. */
    return RT_BE2H_U64(pauFsBlock[cEntries - 1]);
}


/**
 * Locates the extent mapping the file offset in the given extents list.
 *
 * @returns IPRT status.
 * @param   pExtents            The array of extents to search.
 * @param   cEntries            Number of entries in the array.
 * @param   uBlock              The file offset to search the matching mapping for.
 * @param   cBlocks             Number of blocks requested.
 * @param   piBlockFs           Where to store the filesystem block on success.
 * @param   pcBlocks            Where to store the number of contiguous blocks on success.
 * @param   pfSparse            Where to store the sparse flag on success.
 */
DECLINLINE(int) rtFsXfsInode_ExtentLocate(PCXFSEXTENT paExtents, uint16_t cEntries, XFSDFILOFF uBlock,
                                          size_t cBlocks, uint64_t *piBlockFs, size_t *pcBlocks, bool *pfSparse)
{
    int rc = VERR_VFS_BOGUS_FORMAT;

    for (uint32_t i = 0; i < cEntries; i++)
    {
        PCXFSEXTENT pExtent = &paExtents[i];
        uint64_t iBlockExtent = XFS_EXTENT_GET_LOGICAL_BLOCK(pExtent);
        size_t cBlocksExtent = XFS_EXTENT_GET_BLOCK_COUNT(pExtent);

        if (   uBlock >= iBlockExtent
            && uBlock < iBlockExtent + cBlocksExtent)
        {
            uint64_t offExtentBlocks = uBlock - iBlockExtent;
            *piBlockFs = XFS_EXTENT_GET_DISK_BLOCK(pExtent) + offExtentBlocks;
            *pcBlocks  = RT_MIN(cBlocks, cBlocksExtent - offExtentBlocks);
            *pfSparse  = XFS_EXTENT_IS_UNWRITTEN(pExtent);
            rc = VINF_SUCCESS;
            break;
        }
    }

    return rc;
}


/**
 * Validates the given node header.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   pNd                 The node header to validate.
 * @param   iLvl                The current level.
 */
static int rtFsXfsInode_BTreeNdValidate(PRTFSXFSVOL pThis, PCXFSBTREENODEHDR pNd, uint16_t iLvl)
{
    RT_NOREF(pThis, pNd, iLvl);
    /** @todo */
    return VINF_SUCCESS;
}


/**
 * Maps the given inode block to the destination filesystem block.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   pInode              The inode structure to read from.
 * @param   iBlock              The inode block to map.
 * @param   cBlocks             Number of blocks requested.
 * @param   piBlockFs           Where to store the filesystem block on success.
 * @param   pcBlocks            Where to store the number of contiguous blocks on success.
 * @param   pfSparse            Where to store the sparse flag on success.
 *
 * @todo Optimize
 */
static int rtFsXfsInode_MapBlockToFs(PRTFSXFSVOL pThis, PRTFSXFSINODE pInode, uint64_t iBlock, size_t cBlocks,
                                     uint64_t *piBlockFs, size_t *pcBlocks, bool *pfSparse)
{
    int rc = VINF_SUCCESS;

    switch (pInode->enmFormat)
    {
        case XFS_INODE_FORMAT_EXTENTS:
        {
            size_t cbRemaining = 0;
            PCXFSEXTENT paExtents = (PCXFSEXTENT)rtFsXfsInodeGetDataFork(pThis, pInode, &cbRemaining);

            if (cbRemaining <= pInode->cExtentsData * sizeof(XFSEXTENT))
                rc = rtFsXfsInode_ExtentLocate(paExtents, pInode->cExtentsData, cBlocks, iBlock,
                                               piBlockFs, pcBlocks, pfSparse);
            else
                rc = VERR_VFS_BOGUS_FORMAT;
            break;
        }
        case XFS_INODE_FORMAT_BTREE:
        {
            size_t cbRemaining = 0;
            PCXFSBTREEROOTHDR pRoot = (PCXFSBTREEROOTHDR)rtFsXfsInodeGetDataFork(pThis, pInode, &cbRemaining);
            if (cbRemaining >= RT_BE2H_U16(pRoot->cRecs) * (sizeof(XFSDFSBNO) + sizeof(XFSDFILOFF)) + sizeof(XFSBTREEROOTHDR))
            {
                XFSDFILOFF *poffFile = (XFSDFILOFF *)(pRoot + 1);
                XFSDFSBNO  *puFsBlock = (XFSDFSBNO *)(&poffFile[RT_BE2H_U16(pRoot->cRecs)]);

                XFSDFSBNO uFsBlock = rtFsXfsInode_BTreeNdLocateNextLvl(poffFile, puFsBlock, RT_BE2H_U16(pRoot->cRecs),
                                                                       iBlock);
                uint16_t iLvl = RT_BE2H_U16(pRoot->iLvl) - 1;

                /* Resolve intermediate levels. */
                while (   iLvl > 0
                       && RT_SUCCESS(rc))
                {
                    PRTFSXFSBLOCKENTRY pEntry;
                    PCXFSBTREENODEHDR pNd;

                    rc = rtFsXfsVol_BlockLoad(pThis, uFsBlock, &pEntry, (void **)&pNd);
                    if (RT_SUCCESS(rc))
                    {
                        rc = rtFsXfsInode_BTreeNdValidate(pThis, pNd, iLvl);
                        if (RT_SUCCESS(rc))
                        {
                            poffFile = (XFSDFILOFF *)(pNd + 1);
                            puFsBlock = (XFSDFSBNO *)(&poffFile[RT_BE2H_U16(pNd->cRecs)]);
                            uFsBlock = rtFsXfsInode_BTreeNdLocateNextLvl(poffFile, puFsBlock, RT_BE2H_U16(pRoot->cRecs),
                                                                         iBlock);
                            iLvl--;
                        }
                        rtFsXfsVol_BlockRelease(pThis, pEntry);
                    }
                }

                /* Load the leave node and parse it. */
                if (RT_SUCCESS(rc))
                {
                    PRTFSXFSBLOCKENTRY pEntry;
                    PCXFSBTREENODEHDR pNd;

                    rc = rtFsXfsVol_BlockLoad(pThis, uFsBlock, &pEntry, (void **)&pNd);
                    if (RT_SUCCESS(rc))
                    {
                        rc = rtFsXfsInode_BTreeNdValidate(pThis, pNd, iLvl);
                        if (RT_SUCCESS(rc))
                        {
                            PCXFSEXTENT paExtents = (PCXFSEXTENT)(pNd + 1);
                            rc = rtFsXfsInode_ExtentLocate(paExtents, RT_BE2H_U16(pNd->cRecs), cBlocks, iBlock,
                                                           piBlockFs, pcBlocks, pfSparse);
                        }
                        rtFsXfsVol_BlockRelease(pThis, pEntry);
                    }
                }
            }
            else
                rc = VERR_VFS_BOGUS_FORMAT;
            break;
        }
        case XFS_INODE_FORMAT_LOCAL:
        case XFS_INODE_FORMAT_UUID:
        case XFS_INODE_FORMAT_DEV:
        default:
            rc = VERR_VFS_BOGUS_FORMAT;
    }

    return rc;
}


/**
 * Reads data from the given inode at the given byte offset.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   pInode              The inode structure to read from.
 * @param   off                 The byte offset to start reading from.
 * @param   pvBuf               Where to store the read data to.
 * @param   pcbRead             Where to return the amount of data read.
 */
static int rtFsXfsInode_Read(PRTFSXFSVOL pThis, PRTFSXFSINODE pInode, uint64_t off, void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    int rc = VINF_SUCCESS;
    uint8_t *pbBuf = (uint8_t *)pvBuf;

    if (((uint64_t)pInode->ObjInfo.cbObject < off + cbRead))
    {
        if (!pcbRead)
            return VERR_EOF;
        else
            cbRead = (uint64_t)pInode->ObjInfo.cbObject - off;
    }

    if (pInode->enmFormat == XFS_INODE_FORMAT_LOCAL)
    {
        /* Fast path when the data is inlined in the inode. */
        size_t cbRemaining = 0;
        uint8_t *pbSrc = (uint8_t *)rtFsXfsInodeGetDataFork(pThis, pInode, &cbRemaining);
        if (off + cbRemaining <= (uint64_t)pInode->ObjInfo.cbObject)
        {
            memcpy(pvBuf, &pbSrc[off], cbRead);
            *pcbRead = cbRead;
        }
        else
            rc = VERR_VFS_BOGUS_FORMAT;

        return rc;
    }

    while (   cbRead
           && RT_SUCCESS(rc))
    {
        uint64_t iBlockStart   = rtFsXfsDiskOffsetToBlockIdx(pThis, off);
        uint32_t offBlockStart = off % pThis->cbBlock;

        /* Resolve the inode block to the proper filesystem block. */
        uint64_t iBlockFs = 0;
        size_t cBlocks = 0;
        bool fSparse = false;
        rc = rtFsXfsInode_MapBlockToFs(pThis, pInode, iBlockStart, 1, &iBlockFs, &cBlocks, &fSparse);
        if (RT_SUCCESS(rc))
        {
            Assert(cBlocks == 1);

            size_t cbThisRead = RT_MIN(cbRead, pThis->cbBlock - offBlockStart);

            if (!fSparse)
            {
                uint64_t offRead = rtFsXfsBlockIdxToDiskOffset(pThis, iBlockFs);
                rc = RTVfsFileReadAt(pThis->hVfsBacking, offRead + offBlockStart, pbBuf, cbThisRead, NULL);
            }
            else
                memset(pbBuf, 0, cbThisRead);

            if (RT_SUCCESS(rc))
            {
                pbBuf  += cbThisRead;
                cbRead -= cbThisRead;
                off    += cbThisRead;
                if (pcbRead)
                    *pcbRead += cbThisRead;
            }
        }
    }

    return rc;
}



/*
 *
 * File operations.
 * File operations.
 * File operations.
 *
 */

/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtFsXfsFile_Close(void *pvThis)
{
    PRTFSXFSFILE pThis = (PRTFSXFSFILE)pvThis;
    LogFlow(("rtFsXfsFile_Close(%p/%p)\n", pThis, pThis->pInode));

    rtFsXfsInode_Release(pThis->pVol, pThis->pInode);
    pThis->pInode = NULL;
    pThis->pVol   = NULL;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsXfsFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTFSXFSFILE pThis = (PRTFSXFSFILE)pvThis;
    return rtFsXfsInode_QueryInfo(pThis->pInode, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtFsXfsFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTFSXFSFILE pThis = (PRTFSXFSFILE)pvThis;
    AssertReturn(pSgBuf->cSegs == 1, VERR_INTERNAL_ERROR_3);
    RT_NOREF(fBlocking);

    if (off == -1)
        off = pThis->offFile;
    else
        AssertReturn(off >= 0, VERR_INTERNAL_ERROR_3);

    int rc;
    size_t cbRead = pSgBuf->paSegs[0].cbSeg;
    if (!pcbRead)
    {
        rc = rtFsXfsInode_Read(pThis->pVol, pThis->pInode, (uint64_t)off, pSgBuf->paSegs[0].pvSeg, cbRead, NULL);
        if (RT_SUCCESS(rc))
        {
            pThis->offFile = off + cbRead;
            RTSgBufAdvance(pSgBuf, cbRead);
        }
        Log6(("rtFsXfsFile_Read: off=%#RX64 cbSeg=%#x -> %Rrc\n", off, pSgBuf->paSegs[0].cbSeg, rc));
    }
    else
    {
        PRTFSXFSINODE pInode = pThis->pInode;
        if (off >= pInode->ObjInfo.cbObject)
        {
            *pcbRead = 0;
            rc = VINF_EOF;
        }
        else
        {
            if ((uint64_t)off + cbRead <= (uint64_t)pInode->ObjInfo.cbObject)
                rc = rtFsXfsInode_Read(pThis->pVol, pThis->pInode, (uint64_t)off, pSgBuf->paSegs[0].pvSeg, cbRead, NULL);
            else
            {
                /* Return VINF_EOF if beyond end-of-file. */
                cbRead = (size_t)(pInode->ObjInfo.cbObject - off);
                rc = rtFsXfsInode_Read(pThis->pVol, pThis->pInode, off, pSgBuf->paSegs[0].pvSeg, cbRead, NULL);
                if (RT_SUCCESS(rc))
                    rc = VINF_EOF;
            }
            if (RT_SUCCESS(rc))
            {
                pThis->offFile = off + cbRead;
                *pcbRead = cbRead;
                RTSgBufAdvance(pSgBuf, cbRead);
            }
            else
                *pcbRead = 0;
        }
        Log6(("rtFsXfsFile_Read: off=%#RX64 cbSeg=%#x -> %Rrc *pcbRead=%#x\n", off, pSgBuf->paSegs[0].cbSeg, rc, *pcbRead));
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtFsXfsFile_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    RT_NOREF(pvThis, off, pSgBuf, fBlocking, pcbWritten);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtFsXfsFile_Flush(void *pvThis)
{
    RT_NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtFsXfsFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTFSXFSFILE pThis = (PRTFSXFSFILE)pvThis;
    *poffActual = pThis->offFile;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtFsXfsFile_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    RT_NOREF(pvThis, fMode, fMask);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtFsXfsFile_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                              PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RT_NOREF(pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtFsXfsFile_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    RT_NOREF(pvThis, uid, gid);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtFsXfsFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTFSXFSFILE pThis = (PRTFSXFSFILE)pvThis;
    RTFOFF offNew;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            offNew = offSeek;
            break;
        case RTFILE_SEEK_END:
            offNew = pThis->pInode->ObjInfo.cbObject + offSeek;
            break;
        case RTFILE_SEEK_CURRENT:
            offNew = (RTFOFF)pThis->offFile + offSeek;
            break;
        default:
            return VERR_INVALID_PARAMETER;
    }
    if (offNew >= 0)
    {
        pThis->offFile = offNew;
        *poffActual    = offNew;
        return VINF_SUCCESS;
    }
    return VERR_NEGATIVE_SEEK;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtFsXfsFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTFSXFSFILE pThis = (PRTFSXFSFILE)pvThis;
    *pcbFile = (uint64_t)pThis->pInode->ObjInfo.cbObject;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSetSize}
 */
static DECLCALLBACK(int) rtFsXfsFile_SetSize(void *pvThis, uint64_t cbFile, uint32_t fFlags)
{
    RT_NOREF(pvThis, cbFile, fFlags);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQueryMaxSize}
 */
static DECLCALLBACK(int) rtFsXfsFile_QueryMaxSize(void *pvThis, uint64_t *pcbMax)
{
    RT_NOREF(pvThis);
    *pcbMax = INT64_MAX; /** @todo */
    return VINF_SUCCESS;
}


/**
 * XFS file operations.
 */
static const RTVFSFILEOPS g_rtFsXfsFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "XFS File",
            rtFsXfsFile_Close,
            rtFsXfsFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtFsXfsFile_Read,
        rtFsXfsFile_Write,
        rtFsXfsFile_Flush,
        NULL /*PollOne*/,
        rtFsXfsFile_Tell,
        NULL /*pfnSkip*/,
        NULL /*pfnZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        rtFsXfsFile_SetMode,
        rtFsXfsFile_SetTimes,
        rtFsXfsFile_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtFsXfsFile_Seek,
    rtFsXfsFile_QuerySize,
    rtFsXfsFile_SetSize,
    rtFsXfsFile_QueryMaxSize,
    RTVFSFILEOPS_VERSION
};


/**
 * Creates a new VFS file from the given regular file inode.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   fOpen               Open flags passed.
 * @param   iInode              The inode for the file.
 * @param   phVfsFile           Where to store the VFS file handle on success.
 * @param   pErrInfo            Where to record additional error information on error, optional.
 * @param   pszWhat             Logging prefix.
 */
static int rtFsXfsVol_NewFile(PRTFSXFSVOL pThis, uint64_t fOpen, uint32_t iInode,
                              PRTVFSFILE phVfsFile, PRTERRINFO pErrInfo, const char *pszWhat)
{
    /*
     * Load the inode and check that it really is a file.
     */
    PRTFSXFSINODE pInode = NULL;
    int rc = rtFsXfsInode_Load(pThis, iInode, &pInode);
    if (RT_SUCCESS(rc))
    {
        if (RTFS_IS_FILE(pInode->ObjInfo.Attr.fMode))
        {
            PRTFSXFSFILE pNewFile;
            rc = RTVfsNewFile(&g_rtFsXfsFileOps, sizeof(*pNewFile), fOpen, pThis->hVfsSelf, NIL_RTVFSLOCK,
                             phVfsFile, (void **)&pNewFile);
            if (RT_SUCCESS(rc))
            {
                pNewFile->pVol    = pThis;
                pNewFile->pInode  = pInode;
                pNewFile->offFile = 0;
            }
        }
        else
            rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_NOT_A_FILE, "%s: fMode=%#RX32", pszWhat, pInode->ObjInfo.Attr.fMode);

        if (RT_FAILURE(rc))
            rtFsXfsInode_Release(pThis, pInode);
    }

    return rc;
}



/*
 *
 * XFS directory code.
 * XFS directory code.
 * XFS directory code.
 *
 */

/**
 * Looks up an entry in the given directory inode.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   pInode              The directory inode structure to.
 * @param   pszEntry            The entry to lookup.
 * @param   piInode             Where to store the inode number if the entry was found.
 */
static int rtFsXfsDir_Lookup(PRTFSXFSVOL pThis, PRTFSXFSINODE pInode, const char *pszEntry, uint32_t *piInode)
{
    uint64_t offEntry = 0;
    int rc = VERR_FILE_NOT_FOUND;
    uint32_t idxDirEntry = 0;
    size_t cchEntry = strlen(pszEntry);

    if (cchEntry > 255)
        return VERR_FILENAME_TOO_LONG;

    RT_NOREF(pThis, idxDirEntry, offEntry, pInode, piInode);

#if 0 /** @todo */
    while (offEntry < (uint64_t)pInode->ObjInfo.cbObject)
    {
        EXTDIRENTRYEX DirEntry;
        size_t cbThis = RT_MIN(sizeof(DirEntry), (uint64_t)pInode->ObjInfo.cbObject - offEntry);
        int rc2 = rtFsXfsInode_Read(pThis, pInode, offEntry, &DirEntry, cbThis, NULL);
        if (RT_SUCCESS(rc2))
        {
#ifdef LOG_ENABLED
            rtFsExtDirEntry_Log(pThis, idxDirEntry, &DirEntry);
#endif

            uint16_t cbName =   pThis->fFeaturesIncompat & EXT_SB_FEAT_INCOMPAT_DIR_FILETYPE
                              ? DirEntry.Core.u.v2.cbName
                              : RT_LE2H_U16(DirEntry.Core.u.v1.cbName);
            if (   cchEntry == cbName
                && !memcmp(pszEntry, &DirEntry.Core.achName[0], cchEntry))
            {
                *piInode = RT_LE2H_U32(DirEntry.Core.iInodeRef);
                rc = VINF_SUCCESS;
                break;
            }

            offEntry += RT_LE2H_U16(DirEntry.Core.cbRecord);
            idxDirEntry++;
        }
        else
        {
            rc = rc2;
            break;
        }
    }
#endif
    return rc;
}



/*
 *
 * Directory instance methods
 * Directory instance methods
 * Directory instance methods
 *
 */

/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtFsXfsDir_Close(void *pvThis)
{
    PRTFSXFSDIR pThis = (PRTFSXFSDIR)pvThis;
    LogFlowFunc(("pThis=%p\n", pThis));
    rtFsXfsInode_Release(pThis->pVol, pThis->pInode);
    pThis->pInode = NULL;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsXfsDir_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTFSXFSDIR pThis = (PRTFSXFSDIR)pvThis;
    LogFlowFunc(("\n"));
    return rtFsXfsInode_QueryInfo(pThis->pInode, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtFsXfsDir_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    LogFlowFunc(("\n"));
    RT_NOREF(pvThis, fMode, fMask);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtFsXfsDir_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    LogFlowFunc(("\n"));
    RT_NOREF(pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtFsXfsDir_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    LogFlowFunc(("\n"));
    RT_NOREF(pvThis, uid, gid);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpen}
 */
static DECLCALLBACK(int) rtFsXfsDir_Open(void *pvThis, const char *pszEntry, uint64_t fOpen,
                                         uint32_t fFlags, PRTVFSOBJ phVfsObj)
{
    LogFlowFunc(("pszEntry='%s' fOpen=%#RX64 fFlags=%#x\n", pszEntry, fOpen, fFlags));
    PRTFSXFSDIR  pThis = (PRTFSXFSDIR)pvThis;
    PRTFSXFSVOL  pVol  = pThis->pVol;
    int rc = VINF_SUCCESS;

    RT_NOREF(pThis, pVol, phVfsObj, pszEntry, fFlags);

    /*
     * We cannot create or replace anything, just open stuff.
     */
    if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN
        || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE)
    { /* likely */ }
    else
        return VERR_WRITE_PROTECT;

    /*
     * Lookup the entry.
     */
    uint32_t iInode = 0;
    rc = rtFsXfsDir_Lookup(pVol, pThis->pInode, pszEntry, &iInode);
    if (RT_SUCCESS(rc))
    {
        PRTFSXFSINODE pInode = NULL;
        rc = rtFsXfsInode_Load(pVol, iInode, &pInode);
        if (RT_SUCCESS(rc))
        {
            if (RTFS_IS_DIRECTORY(pInode->ObjInfo.Attr.fMode))
            {
                RTVFSDIR hVfsDir;
                rc = rtFsXfsVol_OpenDirByInode(pVol, iInode, &hVfsDir);
                if (RT_SUCCESS(rc))
                {
                    *phVfsObj = RTVfsObjFromDir(hVfsDir);
                    RTVfsDirRelease(hVfsDir);
                    AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                }
            }
            else if (RTFS_IS_FILE(pInode->ObjInfo.Attr.fMode))
            {
                RTVFSFILE hVfsFile;
                rc = rtFsXfsVol_NewFile(pVol, fOpen, iInode, &hVfsFile, NULL, pszEntry);
                if (RT_SUCCESS(rc))
                {
                    *phVfsObj = RTVfsObjFromFile(hVfsFile);
                    RTVfsFileRelease(hVfsFile);
                    AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                }
            }
            else
                rc = VERR_NOT_SUPPORTED;
        }
    }

    LogFlow(("rtFsXfsDir_Open(%s): returns %Rrc\n", pszEntry, rc));
    return rc;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateDir}
 */
static DECLCALLBACK(int) rtFsXfsDir_CreateDir(void *pvThis, const char *pszSubDir, RTFMODE fMode, PRTVFSDIR phVfsDir)
{
    RT_NOREF(pvThis, pszSubDir, fMode, phVfsDir);
    LogFlowFunc(("\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpenSymlink}
 */
static DECLCALLBACK(int) rtFsXfsDir_OpenSymlink(void *pvThis, const char *pszSymlink, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, phVfsSymlink);
    LogFlowFunc(("\n"));
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateSymlink}
 */
static DECLCALLBACK(int) rtFsXfsDir_CreateSymlink(void *pvThis, const char *pszSymlink, const char *pszTarget,
                                                  RTSYMLINKTYPE enmType, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, pszTarget, enmType, phVfsSymlink);
    LogFlowFunc(("\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnUnlinkEntry}
 */
static DECLCALLBACK(int) rtFsXfsDir_UnlinkEntry(void *pvThis, const char *pszEntry, RTFMODE fType)
{
    RT_NOREF(pvThis, pszEntry, fType);
    LogFlowFunc(("\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRenameEntry}
 */
static DECLCALLBACK(int) rtFsXfsDir_RenameEntry(void *pvThis, const char *pszEntry, RTFMODE fType, const char *pszNewName)
{
    RT_NOREF(pvThis, pszEntry, fType, pszNewName);
    LogFlowFunc(("\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRewindDir}
 */
static DECLCALLBACK(int) rtFsXfsDir_RewindDir(void *pvThis)
{
    PRTFSXFSDIR pThis = (PRTFSXFSDIR)pvThis;
    LogFlowFunc(("\n"));

    pThis->fNoMoreFiles = false;
    pThis->offEntry     = 0;
    pThis->idxEntry     = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnReadDir}
 */
static DECLCALLBACK(int) rtFsXfsDir_ReadDir(void *pvThis, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                                            RTFSOBJATTRADD enmAddAttr)
{
    PRTFSXFSDIR     pThis = (PRTFSXFSDIR)pvThis;
    PRTFSXFSINODE   pInode = pThis->pInode;
    LogFlowFunc(("\n"));

    if (pThis->fNoMoreFiles)
        return VERR_NO_MORE_FILES;

    RT_NOREF(pInode, pDirEntry, pcbDirEntry, enmAddAttr);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * XFS directory operations.
 */
static const RTVFSDIROPS g_rtFsXfsDirOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_DIR,
        "XFS Dir",
        rtFsXfsDir_Close,
        rtFsXfsDir_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSDIROPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSDIROPS, ObjSet) - RT_UOFFSETOF(RTVFSDIROPS, Obj),
        rtFsXfsDir_SetMode,
        rtFsXfsDir_SetTimes,
        rtFsXfsDir_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtFsXfsDir_Open,
    NULL /* pfnFollowAbsoluteSymlink */,
    NULL /* pfnOpenFile */,
    NULL /* pfnOpenDir */,
    rtFsXfsDir_CreateDir,
    rtFsXfsDir_OpenSymlink,
    rtFsXfsDir_CreateSymlink,
    NULL /* pfnQueryEntryInfo */,
    rtFsXfsDir_UnlinkEntry,
    rtFsXfsDir_RenameEntry,
    rtFsXfsDir_RewindDir,
    rtFsXfsDir_ReadDir,
    RTVFSDIROPS_VERSION,
};


/**
 * Opens a directory by the given inode.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   iInode              The inode to open.
 * @param   phVfsDir            Where to store the handle to the VFS directory on success.
 */
static int rtFsXfsVol_OpenDirByInode(PRTFSXFSVOL pThis, uint32_t iInode, PRTVFSDIR phVfsDir)
{
    PRTFSXFSINODE pInode = NULL;
    int rc = rtFsXfsInode_Load(pThis, iInode, &pInode);
    if (RT_SUCCESS(rc))
    {
        if (RTFS_IS_DIRECTORY(pInode->ObjInfo.Attr.fMode))
        {
            PRTFSXFSDIR pNewDir;
            rc = RTVfsNewDir(&g_rtFsXfsDirOps, sizeof(*pNewDir), 0 /*fFlags*/, pThis->hVfsSelf, NIL_RTVFSLOCK,
                             phVfsDir, (void **)&pNewDir);
            if (RT_SUCCESS(rc))
            {
                pNewDir->fNoMoreFiles = false;
                pNewDir->pVol         = pThis;
                pNewDir->pInode       = pInode;
            }
        }
        else
            rc = VERR_VFS_BOGUS_FORMAT;

        if (RT_FAILURE(rc))
            rtFsXfsInode_Release(pThis, pInode);
    }

    return rc;
}



/*
 *
 * Volume level code.
 * Volume level code.
 * Volume level code.
 *
 */

static DECLCALLBACK(int) rtFsXfsVolAgTreeDestroy(PAVLU32NODECORE pCore, void *pvUser)
{
    RT_NOREF(pvUser);

    PRTFSXFSAG pAg = (PRTFSXFSAG)pCore;
    Assert(!pAg->cRefs);
    RTMemFree(pAg);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtFsXfsVolInodeTreeDestroy(PAVLU64NODECORE pCore, void *pvUser)
{
    RT_NOREF(pvUser);

    PRTFSXFSINODE pInode = (PRTFSXFSINODE)pCore;
    Assert(!pInode->cRefs);
    RTMemFree(pInode);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtFsXfsVolBlockTreeDestroy(PAVLU64NODECORE pCore, void *pvUser)
{
    RT_NOREF(pvUser);

    PRTFSXFSBLOCKENTRY pBlock = (PRTFSXFSBLOCKENTRY)pCore;
    Assert(!pBlock->cRefs);
    RTMemFree(pBlock);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnClose}
 */
static DECLCALLBACK(int) rtFsXfsVol_Close(void *pvThis)
{
    PRTFSXFSVOL pThis = (PRTFSXFSVOL)pvThis;

    /* Destroy the block group tree. */
    RTAvlU32Destroy(&pThis->AgRoot, rtFsXfsVolAgTreeDestroy, pThis);
    pThis->AgRoot = NULL;
    RTListInit(&pThis->LstAgLru);

    /* Destroy the inode tree. */
    RTAvlU64Destroy(&pThis->InodeRoot, rtFsXfsVolInodeTreeDestroy, pThis);
    pThis->InodeRoot = NULL;
    RTListInit(&pThis->LstInodeLru);

    /* Destroy the block cache tree. */
    RTAvlU64Destroy(&pThis->BlockRoot, rtFsXfsVolBlockTreeDestroy, pThis);
    pThis->BlockRoot = NULL;
    RTListInit(&pThis->LstBlockLru);

    /*
     * Backing file and handles.
     */
    RTVfsFileRelease(pThis->hVfsBacking);
    pThis->hVfsBacking = NIL_RTVFSFILE;
    pThis->hVfsSelf    = NIL_RTVFS;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsXfsVol_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RT_NOREF(pvThis, pObjInfo, enmAddAttr);
    return VERR_WRONG_TYPE;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnOpenRoot}
 */
static DECLCALLBACK(int) rtFsXfsVol_OpenRoot(void *pvThis, PRTVFSDIR phVfsDir)
{
    PRTFSXFSVOL pThis = (PRTFSXFSVOL)pvThis;
    int rc = rtFsXfsVol_OpenDirByInode(pThis, pThis->uInodeRoot, phVfsDir);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryRangeState}
 */
static DECLCALLBACK(int) rtFsXfsVol_QueryRangeState(void *pvThis, uint64_t off, size_t cb, bool *pfUsed)
{
    RT_NOREF(pvThis, off, cb, pfUsed);
    return VERR_NOT_IMPLEMENTED;
}


DECL_HIDDEN_CONST(const RTVFSOPS) g_rtFsXfsVolOps =
{
    /* .Obj = */
    {
        /* .uVersion = */       RTVFSOBJOPS_VERSION,
        /* .enmType = */        RTVFSOBJTYPE_VFS,
        /* .pszName = */        "XfsVol",
        /* .pfnClose = */       rtFsXfsVol_Close,
        /* .pfnQueryInfo = */   rtFsXfsVol_QueryInfo,
        /* .pfnQueryInfoEx = */ NULL,
        /* .uEndMarker = */     RTVFSOBJOPS_VERSION
    },
    /* .uVersion = */           RTVFSOPS_VERSION,
    /* .fFeatures = */          0,
    /* .pfnOpenRoot = */        rtFsXfsVol_OpenRoot,
    /* .pfnQueryRangeState = */ rtFsXfsVol_QueryRangeState,
    /* .uEndMarker = */         RTVFSOPS_VERSION
};


/**
 * Loads and parses the AGI block.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtFsXfsVolLoadAgi(PRTFSXFSVOL pThis, PRTERRINFO pErrInfo)
{
    XFSAGI Agi;
    int rc = RTVfsFileReadAt(pThis->hVfsBacking, 2 * pThis->cbSector, &Agi, sizeof(&Agi), NULL);
    if (RT_SUCCESS(rc))
    {
#ifdef LOG_ENABLED
        rtFsXfsAgi_Log(0, &Agi);
#endif

        /** @todo Verification */
        RT_NOREF(pErrInfo);
    }

    return rc;
}


/**
 * Loads and parses the superblock of the filesystem.
 *
 * @returns IPRT status code.
 * @param   pThis               The XFS volume instance.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtFsXfsVolLoadAndParseSuperblock(PRTFSXFSVOL pThis, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;
    XFSSUPERBLOCK Sb;
    rc = RTVfsFileReadAt(pThis->hVfsBacking, XFS_SB_OFFSET, &Sb, sizeof(XFSSUPERBLOCK), NULL);
    if (RT_FAILURE(rc))
        return RTERRINFO_LOG_SET(pErrInfo, rc, "Error reading super block");

    /* Validate the superblock. */
    if (RT_BE2H_U32(Sb.u32Magic) != XFS_SB_MAGIC)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT, "Not XFS - Signature mismatch: %RX32", RT_BE2H_U32(Sb.u32Magic));

#ifdef LOG_ENABLED
    rtFsXfsSb_Log(0, &Sb);
#endif

    /** @todo More verification */
    pThis->cbSector           = RT_BE2H_U32(Sb.cbSector);
    pThis->cbBlock            = RT_BE2H_U32(Sb.cbBlock);
    pThis->cBlockShift        = Sb.cBlockSzLog;
    pThis->cBlocksPerAg       = RT_BE2H_U32(Sb.cAgBlocks);
    pThis->cAgs               = RT_BE2H_U32(Sb.cAg);
    pThis->uInodeRoot         = RT_BE2H_U64(Sb.uInodeRoot);
    pThis->cbInode            = RT_BE2H_U16(Sb.cbInode);
    pThis->cInodesPerBlock    = RT_BE2H_U16(Sb.cInodesPerBlock);
    pThis->cAgBlocksLog       = Sb.cAgBlocksLog;
    pThis->cInodesPerBlockLog = Sb.cInodesPerBlockLog;
    return rc;
}


RTDECL(int) RTFsXfsVolOpen(RTVFSFILE hVfsFileIn, uint32_t fMntFlags, uint32_t fXfsFlags, PRTVFS phVfs, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(phVfs, VERR_INVALID_POINTER);
    AssertReturn(!(fMntFlags & ~RTVFSMNT_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(!fXfsFlags, VERR_INVALID_FLAGS);

    uint32_t cRefs = RTVfsFileRetain(hVfsFileIn);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create a VFS instance and initialize the data so rtFsXfsVol_Close works.
     */
    RTVFS       hVfs;
    PRTFSXFSVOL pThis;
    int rc = RTVfsNew(&g_rtFsXfsVolOps, sizeof(*pThis), NIL_RTVFS, RTVFSLOCK_CREATE_RW, &hVfs, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsBacking    = hVfsFileIn;
        pThis->hVfsSelf       = hVfs;
        pThis->fMntFlags      = fMntFlags;
        pThis->fXfsFlags      = fXfsFlags;
        pThis->AgRoot         = NULL;
        pThis->InodeRoot      = NULL;
        pThis->BlockRoot      = NULL;
        pThis->cbAgs          = 0;
        pThis->cbInodes       = 0;
        pThis->cbBlocks       = 0;
        RTListInit(&pThis->LstAgLru);
        RTListInit(&pThis->LstInodeLru);
        RTListInit(&pThis->LstBlockLru);

        rc = RTVfsFileQuerySize(pThis->hVfsBacking, &pThis->cbBacking);
        if (RT_SUCCESS(rc))
        {
            rc = rtFsXfsVolLoadAndParseSuperblock(pThis, pErrInfo);
            if (RT_SUCCESS(rc))
                rc = rtFsXfsVolLoadAgi(pThis, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                *phVfs = hVfs;
                return VINF_SUCCESS;
            }
        }

        RTVfsRelease(hVfs);
        *phVfs = NIL_RTVFS;
    }
    else
        RTVfsFileRelease(hVfsFileIn);

    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainXfsVol_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                                   PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg);

    /*
     * Basic checks.
     */
    if (pElement->enmTypeIn != RTVFSOBJTYPE_FILE)
        return pElement->enmTypeIn == RTVFSOBJTYPE_INVALID ? VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT : VERR_VFS_CHAIN_TAKES_FILE;
    if (   pElement->enmType != RTVFSOBJTYPE_VFS
        && pElement->enmType != RTVFSOBJTYPE_DIR)
        return VERR_VFS_CHAIN_ONLY_DIR_OR_VFS;
    if (pElement->cArgs > 1)
        return VERR_VFS_CHAIN_AT_MOST_ONE_ARG;

    /*
     * Parse the flag if present, save in pElement->uProvider.
     */
    bool fReadOnly = (pSpec->fOpenFile & RTFILE_O_ACCESS_MASK) == RTFILE_O_READ;
    if (pElement->cArgs > 0)
    {
        const char *psz = pElement->paArgs[0].psz;
        if (*psz)
        {
            if (!strcmp(psz, "ro"))
                fReadOnly = true;
            else if (!strcmp(psz, "rw"))
                fReadOnly = false;
            else
            {
                *poffError = pElement->paArgs[0].offSpec;
                return RTErrInfoSet(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT, "Expected 'ro' or 'rw' as argument");
            }
        }
    }

    pElement->uProvider = fReadOnly ? RTVFSMNT_F_READ_ONLY : 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) rtVfsChainXfsVol_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                      PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                      PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, poffError);

    int         rc;
    RTVFSFILE   hVfsFileIn = RTVfsObjToFile(hPrevVfsObj);
    if (hVfsFileIn != NIL_RTVFSFILE)
    {
        RTVFS hVfs;
        rc = RTFsXfsVolOpen(hVfsFileIn, (uint32_t)pElement->uProvider, (uint32_t)(pElement->uProvider >> 32), &hVfs, pErrInfo);
        RTVfsFileRelease(hVfsFileIn);
        if (RT_SUCCESS(rc))
        {
            *phVfsObj = RTVfsObjFromVfs(hVfs);
            RTVfsRelease(hVfs);
            if (*phVfsObj != NIL_RTVFSOBJ)
                return VINF_SUCCESS;
            rc = VERR_VFS_CHAIN_CAST_FAILED;
        }
    }
    else
        rc = VERR_VFS_CHAIN_CAST_FAILED;
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) rtVfsChainXfsVol_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                           PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                           PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pReuseSpec);
    if (   pElement->paArgs[0].uProvider == pReuseElement->paArgs[0].uProvider
        || !pReuseElement->paArgs[0].uProvider)
        return true;
    return false;
}


/** VFS chain element 'xfs'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainXfsVolReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "xfs",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Open a XFS file system, requires a file object on the left side.\n"
                                "First argument is an optional 'ro' (read-only) or 'rw' (read-write) flag.\n",
    /* pfnValidate = */         rtVfsChainXfsVol_Validate,
    /* pfnInstantiate = */      rtVfsChainXfsVol_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainXfsVol_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainXfsVolReg, rtVfsChainXfsVolReg);

