/* $Id: xfs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, XFS format.
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

#ifndef IPRT_INCLUDED_formats_xfs_h
#define IPRT_INCLUDED_formats_xfs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_xfs    XFS filesystem structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */

/*
 * The filesystem structures were retrieved from:
 * http://xfs.org/docs/xfsdocs-xml-dev/XFS_Filesystem_Structure//tmp/en-US/html/index.html and
 * https://elixir.bootlin.com/linux/v4.9/source/fs/xfs/libxfs/xfs_format.h and
 * https://righteousit.wordpress.com/
 */

/** XFS superblock offset from the beginning of the volume, this is constant. */
#define XFS_SB_OFFSET               UINT64_C(0)

/** @name Common XFS types as defined in the spec.
 * @{ */
/** Unsigned 64 bit absolute inode number. */
typedef uint64_t XFSINO;
/** Signed 64 bit file offset. */
typedef int64_t XFSFOFF;
/** Signed 64 bit disk address. */
typedef int64_t XFSDADDR;
/** Unsinged 32 bit allocation group (AG) number. */
typedef uint32_t XFSAGNUMBER;
/** Unsigned 32 bit AG relative block number. */
typedef uint32_t XFSAGBLOCK;
/** Unsigned 32 bit extent length in blocks. */
typedef uint32_t XFSEXTLEN;
/** Signed 32 bit number of extents in a file. */
typedef int32_t XFSEXTNUM;
/** Unsigned 32 bit block number for directories and extended attributes. */
typedef uint32_t XFSDABLK;
/** Unsigned 32 bit hash of a directory file name or extended attribute name. */
typedef uint32_t XFSDAHASH;
/** Unsigned 64 bit filesystem block number combining AG number and block offset into the AG. */
typedef uint64_t XFSDFSBNO;
/** Unsigned 64 bit raw filesystem block number. */
typedef uint64_t XFSDRFSBNO;
/** Unsigned 64 bit extent number in the real-time device. */
typedef uint64_t XFSDRTBNO;
/** Unsigned 64 bit block offset int oa file. */
typedef uint64_t XFSDFILOFF;
/** Unsigned 64 bit block count for a file. */
typedef uint64_t XFSDFILBLKS;
/** @} */

/**
 * XFS superblock.
 */
#pragma pack(1)
typedef struct XFSSUPERBLOCK
{
    /** 0x00: Magic number to identify the superblock. */
    uint32_t    u32Magic;
    /** 0x04: Size of smallest allocation unit in bytes. */
    uint32_t    cbBlock;
    /** 0x04: Number of blocks available for data and metadata. */
    XFSDRFSBNO  cBlocks;
    /** 0x0c: Number of block in the real-time device. */
    XFSDRFSBNO  cBlocksRtDev;
    /** 0x14: Number of extents on real-time device. */
    XFSDRTBNO   cExtentsRtDev;
    /** 0x1c: UUID of the filesystem. */
    uint8_t     abUuid[16];
    /** 0x2c: First block of the filesystem journal. */
    XFSDFSBNO   uBlockJournal;
    /** 0x34: Inode number of the root directory. */
    XFSINO      uInodeRoot;
    /** Inode for the real-time extent bitmap. */
    XFSINO      uInodeBitmapRtExt;
    /** Inode for the real-time bitmap summary. */
    XFSINO      uInodeBitmapSummary;
    /** Extent size on the real-time device in blocks. */
    XFSAGBLOCK  cRtExtent;
    /** Size of an AG in blocks. */
    XFSAGBLOCK  cAgBlocks;
    /** Number of AGs in hte filesystem. */
    XFSAGNUMBER cAg;
    /** Number of real-time bitmap blocks. */
    XFSEXTLEN   cRtBitmapBlocks;
    /** Number of blocks for the journal. */
    XFSEXTLEN   cJournalBlocks;
    /** Version number (actually flag bitmaps of features). */
    uint16_t    fVersion;
    /** Sector size of the underlying medium. */
    uint16_t    cbSector;
    /** Size of an inode in bytes. */
    uint16_t    cbInode;
    /** Number of inodes stored in one block. */
    uint16_t    cInodesPerBlock;
    /** Name of the filesystem. */
    char        achFsName[12];
    /** Block size as log2 (number of bits to shift left). */
    uint8_t     cBlockSzLog;
    /** Sector size as log2 (number of bits to shift left). */
    uint8_t     cSectorSzLog;
    /** Inode size as log2 (number of bits to shift left). */
    uint8_t     cInodeSzLog;
    /** Number of inodes per block as log2.  */
    uint8_t     cInodesPerBlockLog;
    /** Number of AG blocks as log2 (number of bits to shift left). */
    uint8_t     cAgBlocksLog;
    /** Number of extent blocks as log2. */
    uint8_t     cExtentsRtDevLog;
    /** Flag when the filesystem is in the process of being created. */
    uint8_t     fInProgress;
    /** Maximum percentage of the filesystem usable for inodes. */
    uint8_t     cInodeMaxPct;
    /** Global number of inodes allocated (only mainted on the first superblock). */
    uint64_t    cInodesGlobal;
    /** Global number of free inodes (only mainted on the first superblock). */
    uint64_t    cInodesGlobalFree;
    /** Global count of free data blocks on the filesystem (only mainted on the first superblock). */
    uint64_t    cBlocksFree;
    /** Global count of free extents on the real-time device (only mainted on the first superblock). */
    uint64_t    cExtentsRtFree;
    /** Inode containing the user quotas. */
    XFSINO      uInodeQuotaUsr;
    /** Inode containing the group/project quotas. */
    XFSINO      uInodeQuotaGrp;
    /** Quota flags. */
    uint16_t    fQuotaFlags;
    /** Misc flags. */
    uint8_t     fFlagsMisc;
    /** Reserved MBZ. */
    uint8_t     uSharedVn;
    /** Number of filesystem blocks for the inode chunk alignment. */
    XFSEXTLEN   cBlocksInodeAlignment;
    /** Raid stripe size in blocks. */
    uint32_t    cBlocksRaidStripe;
    /** Raid width in number of blocks. */
    uint32_t    cBlocksRaidWidth;
    /** Multiplier for determining the allocation size for directory blocks as log2. */
    uint8_t     cDirBlockAllocLog;
    /** Sub volume sector size as log2 if an external journal device is used. */
    uint8_t     cLogDevSubVolSectorSzLog;
    /** Sector size of the device an external journal is stored as log2. */
    uint16_t    cLogDevSectorSzLog;
    /** Log devices stripe size. */
    uint32_t    cLogDevRaidStripe;
    /** Additional features which may be active. */
    uint32_t    fFeatures2;
    /** Padding. */
    uint32_t    u32Padding0;
    /** From here follow data only available from version 5 and later. */
    /** Read/Write feature flags. */
    uint32_t    fFeaturesRw;
    /** Read-only feature flags. */
    uint32_t    fFeaturesRo;
    /** Read/Write incompatible feature flags. */
    uint32_t    fFeaturesIncompatRw;
    /** Read/Write incompatible feature flags for the journal. */
    uint32_t    fFeaturesJrnlIncompatRw;
    /** CRC32 checksum for the superblock. */
    uint32_t    u32Chksum;
    /** Sparse inode alignment. */
    uint32_t    u32SparseInodeAlignment;
    /** Project quota inode. */
    XFSINO      uInodeProjectQuota;
    /** Log sequence number of last superblock update. */
    uint64_t    uJrnlSeqSbUpdate;
    /** UUID used when INCOMPAT_META_UUID is used. */
    uint8_t     abUuidMeta[16];
    /** Inode if INCOMPATMETA_RMAPBT is used. */
    XFSINO      uInodeRm;
} XFSSUPERBLOCK;
#pragma pack()
AssertCompileSize(XFSSUPERBLOCK, 272);
/** Pointer to an XFS superblock. */
typedef XFSSUPERBLOCK *PXFSSUPERBLOCK;
/** Pointer to a const XFS superblock. */
typedef const XFSSUPERBLOCK *PCXFSSUPERBLOCK;

/** XFS superblock magic. */
#define XFS_SB_MAGIC                                 RT_MAKE_U32_FROM_U8('B', 'S', 'F', 'X')

/** @name XFS_SB_VERSION_F_XXX - Version/Feature flags.
 * @{ */
/** Retrieves the version part of the field. */
#define XFS_SB_VERSION_GET(a_fVersion)               ((a_fVersion) & 0xf)
/** Version number for filesystem 5.3, 6.0.1 and 6.1. */
#define XFS_SB_VERSION_1                             1
/** Version number for filesystem 6.2 - attributes. */
#define XFS_SB_VERSION_2                             2
/** Version number for filesystem 6.2 - new inode version. */
#define XFS_SB_VERSION_3                             3
/** Version number for filesystem 6.2+ - new bitmask version. */
#define XFS_SB_VERSION_4                             4
/** Introduced checksums in the metadata. */
#define XFS_SB_VERSION_5                             5
/** Extended attributes are used for at least one inode. */
#define XFS_SB_VERSION_F_ATTR                        RT_BIT_32(4)
/** At least one inode use 32-bit nlink values. */
#define XFS_SB_VERSION_F_NLINK                       RT_BIT_32(5)
/** Quotas are enabled on the filesystem. */
#define XFS_SB_VERSION_F_QUOTA                       RT_BIT_32(6)
/** Set if XFSSUPERBLOCK::cBlocksInodeAlignment is used. */
#define XFS_SB_VERSION_F_ALIGN                       RT_BIT_32(7)
/** Set if XFSSUPERBLOCK::cBlocksRaidStripe and XFSSUPERBLOCK::cBlocksRaidWidth are used. */
#define XFS_SB_VERSION_F_DALIGN                      RT_BIT_32(8)
/** Set if XFSSUPERBLOCK::uSharedVn is used. */
#define XFS_SB_VERSION_F_SHARED                      RT_BIT_32(9)
/** Version 2 journaling is used. */
#define XFS_SB_VERSION_F_LOGV2                       RT_BIT_32(10)
/** Set if sector size is not 512 bytes. */
#define XFS_SB_VERSION_F_SECTOR                      RT_BIT_32(11)
/** Set if unwritten extents are used (always set). */
#define XFS_SB_VERSION_F_EXTFLG                      RT_BIT_32(12)
/** Version 2 directories are used (always set). */
#define XFS_SB_VERSION_F_DIRV2                       RT_BIT_32(13)
/** Set if XFSSUPERBLOCK::fFeatures2 is used. */
#define XFS_SB_VERSION_F_FEAT2                       RT_BIT_32(14)
/** @} */

/** @name XFS_SB_QUOTA_F_XXX - Quota flags
 * @{ */
/** User quota accounting enabled. */
#define XFS_SB_QUOTA_F_USR_ACCT                      RT_BIT(0)
/** User quotas are enforced. */
#define XFS_SB_QUOTA_F_USR_ENFD                      RT_BIT(1)
/** User quotas have been checked and updated on disk. */
#define XFS_SB_QUOTA_F_USR_CHKD                      RT_BIT(2)
/** Project quota accounting is enabled. */
#define XFS_SB_QUOTA_F_PROJ_ACCT                     RT_BIT(3)
/** Other quotas are enforced. */
#define XFS_SB_QUOTA_F_OTH_ENFD                      RT_BIT(4)
/** Other quotas have been checked and updated on disk. */
#define XFS_SB_QUOTA_F_OTH_CHKD                      RT_BIT(5)
/** Group quota accounting enabled. */
#define XFS_SB_QUOTA_F_GRP_ACCT                      RT_BIT(6)
/** @} */

/** @name XFS_SB_FEATURES2_F_XXX - Additional features
 * @{ */
/** Global counters are lazy and are only updated when the filesystem is cleanly unmounted. */
#define XFS_SB_FEATURES2_F_LAZYSBCOUNT               RT_BIT_32(1)
/** Extended attributes version 2. */
#define XFS_SB_FEATURES2_F_ATTR2                     RT_BIT_32(3)
/** Parent pointers, inodes must have an extended attribute pointing to the parent inode. */
#define XFS_SB_FEATURES2_F_PARENT                    RT_BIT_32(4)
/** @} */


/**
 * XFS AG free space block.
 */
typedef struct XFSAGF
{
    /** Magic number. */
    uint32_t    u32Magic;
    /** Header version number. */
    uint32_t    uVersion;
    /** AG number for the sector. */
    uint32_t    uSeqNo;
    /** Length of the AG in filesystem blocks. */
    uint32_t    cLengthBlocks;
    /** Block numbers for the roots of the free space B+trees. */
    uint32_t    auRoots[3];
    /** Depths of the free space B+trees. */
    uint32_t    acLvls[3];
    /** Index of the first free list block. */
    uint32_t    idxFreeListFirst;
    /** Index of the last free list block. */
    uint32_t    idxFreeListLast;
    /** Number of blocks in the free list. */
    uint32_t    cFreeListBlocks;
    /** Current number of free blocks in the AG. */
    uint32_t    cFreeBlocks;
    /** Longest number of contiguous free blocks in the AG. */
    uint32_t    cFreeBlocksLongest;
    /** Number of blocks used for the free space B+-trees. */
    uint32_t    cBlocksBTrees;
    /** UUID of filesystem the AG belongs to. */
    uint8_t     abUuid[16];
    /** Number of blocks used for the reverse map. */
    uint32_t    cBlocksRevMap;
    /** Number of blocks used for the refcount B+-tree. */
    uint32_t    cBlocksRefcountBTree;
    /** Block number for the refcount tree root. */
    uint32_t    uRootRefcount;
    /** Depth of the refcount B+-tree. */
    uint32_t    cLvlRefcount;
    /** Reserved contiguous space for future extensions. */
    uint64_t    au64Rsvd[14];
    /** Last write sequence number. */
    uint64_t    uSeqNoLastWrite;
    /** CRC of the AGF. */
    uint32_t    uChkSum;
    /** Padding to 64 bit alignment. */
    uint32_t    uAlignment0;
} XFSAGF;
/** Pointer to a AG free space block. */
typedef XFSAGF *PXFSAGF;
/** Poiner to a const AG free space block. */
typedef const XFSAGF *PCXFSAGF;

/** AGF magic. */
#define XFS_AGF_MAGIC                                RT_MAKE_U32_FROM_U8('F', 'G', 'A', 'X')
/** The current valid AGF version. */
#define XFS_AGF_VERSION                              1


/**
 * XFS AG inode information.
 */
typedef struct XFSAGI
{
    /** Magic number. */
    uint32_t    u32Magic;
    /** Header version number. */
    uint32_t    uVersion;
   /** AG number for the sector. */
    uint32_t    uSeqNo;
    /** Length of the AG in filesystem blocks. */
    uint32_t    cLengthBlocks;
    /** Count of allocated inodes. */
    uint32_t    cInodesAlloc;
    /** Block number of the inode tree root. */
    uint32_t    uRootInode;
    /** Depth of the inode B+-tree. */
    uint32_t    cLvlsInode;
    /** Newest allocated inode. */
    uint32_t    uInodeNew;
    /** Last directory inode chunk. */
    uint32_t    uInodeDir;
    /** Hash table of unlinked but still referenced inodes. */
    uint32_t    au32HashUnlinked[64];
    /** UUID of filesystem. */
    uint8_t     abUuid[16];
    /** CRC of the AGI. */
    uint32_t    uChkSum;
    /** Padding. */
    uint32_t    uAlignment0;
    /** Last write sequence number. */
    uint64_t    uSeqNoLastWrite;
    /** Block number of the free inode tree. */
    uint32_t    uRootFreeInode;
    /** Depth of the free inode B+-tree. */
    uint32_t    cLvlsFreeInode;
} XFSAGI;
/** Pointer to a AG inode information. */
typedef XFSAGI *PXFSAGI;
/** Pointer to a const AG inode information. */
typedef const XFSAGI *PCXFSAGI;

/** AGI magic. */
#define XFS_AGI_MAGIC                                RT_MAKE_U32_FROM_U8('I', 'G', 'A', 'X')
/** The current valid AGI version. */
#define XFS_AGI_VERSION                              1


/**
 * XFS timestamp structure.
 */
typedef struct XFSTIMESTAMP
{
    /** 0x00: The second part of the timestamp since the epoch. */
    int32_t    cSecEpoch;
    /** 0x04: Nanosecond part of the timestamp. */
    int32_t    cNanoSec;
} XFSTIMESTAMP;
/** Pointer to a XFS timestamp. */
typedef XFSTIMESTAMP *PXFSTIMESTAMP;
/** Poiner to a const CFS timestamp. */
typedef const XFSTIMESTAMP *PCXFSTIMESTAMP;


/**
 * The inode core structure.
 */
typedef struct XFSINODECORE
{
    /** 0x00: Magic value. */
    uint16_t     u16Magic;
    /** 0x02: File mode and access bits (XFS_INODE_MODE_XXX). */
    uint16_t     fMode;
    /** 0x04: Inode version. */
    int8_t       iVersion;
    /** 0x05: The format of the data fork. */
    int8_t       enmFormat;
    /** 0x06: Number of links to this inode from directories for v1 inodes. */
    uint16_t     cOnLinks;
    /** 0x08: Owners UID. */
    uint32_t     uUid;
    /** 0x0c: Owners GID. */
    uint32_t     uGid;
    /** 0x10: The number of links to this inode for v2 inodes. */
    uint32_t     cLinks;
    /** 0x14: Project ID for v2 inodes (not used for v1, low 16bits). */
    uint16_t     uProjIdLow;
    /** 0x16: Project ID for v2 inodes (not used for v1, high 16bits). */
    uint16_t     uProjIdHigh;
    /** 0x18: Padding. */
    uint8_t      abPad0[6];
    /** 0x1e: Flush counter. */
    uint16_t     cFlush;
    /** 0x20: Last accessed timestamp. */
    XFSTIMESTAMP TsLastAccessed;
    /** 0x28: Last modified timestamp. */
    XFSTIMESTAMP TsLastModified;
    /** 0x30: Inode created/modified timestamp. */
    XFSTIMESTAMP TsCreatedModified;
    /** 0x38: Number of bytes in the file. */
    uint64_t     cbInode;
    /** 0x40: Number of direct and B-Tree blocks used for the forks. */
    uint64_t     cBlocks;
    /** 0x48: Minimum extent size for the inode. */
    uint32_t     cExtentBlocksMin;
    /** 0x4c: Number of extents in the data fork. */
    uint32_t     cExtentsData;
    /** 0x50: Number of extents in the attribute fork. */
    uint16_t     cExtentsAttr;
    /** 0x52: Offset of the attribute fork from the start of the inode. */
    uint8_t      offAttrFork;
    /** 0x53: Attribute fork format. */
    int8_t       enmFormatAttr;
    /** 0x54: DMIG event mask. */
    uint32_t     fEvtMaskDmig;
    /** 0x58: DMIG state info. */
    uint16_t     uStateDmig;
    /** 0x5a: Inode flags. */
    uint16_t     fFlags;
    /** 0x5c: Generation number. */
    uint32_t     cGeneration;
    /** 0x60: AGI unlinked list pointer. */
    uint32_t     offBlockUnlinkedNext;
    /** The following fields are for v3 inodes only. */
    /** 0x64: The CRC of the inode. */
    uint32_t     uChkSum;
    /** 0x68: Number of attribute changes. */
    uint64_t     cAttrChanges;
    /** 0x70: Last flush sequence number. */
    uint64_t     uFlushSeqNo;
    /** 0x78: Additional flags. */
    uint64_t     fFlags2;
    /** 0x80: Basic COW extent size. */
    uint32_t     cExtentCowMin;
    /** 0x84: Padding for future expansion. */
    uint8_t      abPad1[12];
    /** 0x90: Inode creation timestamp. */
    XFSTIMESTAMP TsCreation;
    /** 0x98: The inode number. */
    uint64_t     uInode;
    /** 0x100: Filesystem UUID the inode belongs to. */
    uint8_t      abUuid[16];
} XFSINODECORE;
AssertCompileSizeAlignment(XFSINODECORE, 8);
/** Pointer to a inode core. */
typedef XFSINODECORE *PXFSINODECORE;
/** Pointer to a const inode core. */
typedef const XFSINODECORE *PCXFSINODECORE;

/** Inode magic. */
#define XFS_INODE_MAGIC                              RT_MAKE_U16_FROM_U8('N', 'I')

/** @name XFS_INODE_MODE_XXX - File mode
 * @{ */
/** Others can execute the file. */
#define XFS_INODE_MODE_EXEC_OTHER                    RT_BIT(0)
/** Others can write to the file. */
#define XFS_INODE_MODE_WRITE_OTHER                   RT_BIT(1)
/** Others can read the file. */
#define XFS_INODE_MODE_READ_OTHER                    RT_BIT(2)
/** Members of the same group can execute the file. */
#define XFS_INODE_MODE_EXEC_GROUP                    RT_BIT(3)
/** Members of the same group can write to the file. */
#define XFS_INODE_MODE_WRITE_GROUP                   RT_BIT(4)
/** Members of the same group can read the file. */
#define XFS_INODE_MODE_READ_GROUP                    RT_BIT(5)
/** Owner can execute the file. */
#define XFS_INODE_MODE_EXEC_OWNER                    RT_BIT(6)
/** Owner can write to the file. */
#define XFS_INODE_MODE_WRITE_OWNER                   RT_BIT(7)
/** Owner can read the file. */
#define XFS_INODE_MODE_READ_OWNER                    RT_BIT(8)
/** Sticky file mode. */
#define XFS_INODE_MODE_STICKY                        RT_BIT(9)
/** File is set GID. */
#define XFS_INODE_MODE_SET_GROUP_ID                  RT_BIT(10)
/** File is set UID. */
#define XFS_INODE_MODE_SET_USER_ID                   RT_BIT(11)
/** @} */

/** @name XFS_INODE_MODE_TYPE_XXX - File type
 * @{ */
/** Inode represents a FIFO. */
#define XFS_INODE_MODE_TYPE_FIFO                     UINT16_C(0x1000)
/** Inode represents a character device. */
#define XFS_INODE_MODE_TYPE_CHAR                     UINT16_C(0x2000)
/** Inode represents a directory. */
#define XFS_INODE_MODE_TYPE_DIR                      UINT16_C(0x4000)
/** Inode represents a block device. */
#define XFS_INODE_MODE_TYPE_BLOCK                    UINT16_C(0x6000)
/** Inode represents a regular file. */
#define XFS_INODE_MODE_TYPE_REGULAR                  UINT16_C(0x8000)
/** Inode represents a symlink. */
#define XFS_INODE_MODE_TYPE_SYMLINK                  UINT16_C(0xa000)
/** Inode represents a socket. */
#define XFS_INODE_MODE_TYPE_SOCKET                   UINT16_C(0xc000)
/** Returns the inode type from the combined mode field. */
#define XFS_INODE_MODE_TYPE_GET_TYPE(a_Mode)         ((a_Mode) & 0xf000)
/** @} */

/** @name XFS_INODE_FORMAT_XXX - Inode data fork format.
 * @{ */
/** Device node data. */
#define XFS_INODE_FORMAT_DEV                        0
/** Inline data. */
#define XFS_INODE_FORMAT_LOCAL                      1
/** Array of extent descriptors. */
#define XFS_INODE_FORMAT_EXTENTS                    2
/** Data fork contains root of B-Tree. */
#define XFS_INODE_FORMAT_BTREE                      3
/** Data fork contains UUID. */
#define XFS_INODE_FORMAT_UUID                       4
/** @} */

/** @name XFS_INODE_F_XXX - Inode flags.
 * @{ */
/** File data blocks are stored in the real-time device area. */
#define XFS_INODE_F_RTDEV                            RT_BIT(0)
/** File space has been pre-allocated. */
#define XFS_INODE_F_PREALLOC                         RT_BIT(1)
/** Use new real-time bitmap format. */
#define XFS_INODE_F_NEWRTBITMAP                      RT_BIT(2)
/** Inode is immutable. */
#define XFS_INODE_F_IMMUTABLE                        RT_BIT(3)
/** Inode is append only. */
#define XFS_INODE_F_APPEND                           RT_BIT(4)
/** Inode is written synchronously. */
#define XFS_INODE_F_SYNC                             RT_BIT(5)
/** The last accessed timestamp is not updated. */
#define XFS_INODE_F_NOATIME                          RT_BIT(6)
/** The inode is not dumpable via dump(1). */
#define XFS_INODE_F_NODUMP                           RT_BIT(7)
/** Create with real-time bit set. */
#define XFS_INODE_F_RTINHERIT                        RT_BIT(8)
/** Create with parents project ID. */
#define XFS_INODE_F_PROJIDINHERIT                    RT_BIT(9)
/** Deny symlink creation. */
#define XFS_INODE_F_NOSYMLINKS                       RT_BIT(10)
/** Inode extent size allocator hint. */
#define XFS_INODE_F_EXTSIZEHINT                      RT_BIT(11)
/** Inode extent size is inherited. */
#define XFS_INODE_F_EXTSIZEINHERIT                   RT_BIT(12)
/** Do not defrag/reorganize the inode. */
#define XFS_INODE_F_NODEFRAG                         RT_BIT(13)
/** Use filestream allocator. */
#define XFS_INODE_F_FILESTREAM                       RT_BIT(14)
/** @} */

/** @name XFS_INODE_F2_XXX - Inode flags number 2 (XFSINODECORE::fFlags2).
 * @{ */
/** Use DAX for the inode. */
#define XFS_INODE_F2_DAX                             RT_BIT_64(0)
/** Blocks use reference counting for sharing. */
#define XFS_INODE_F2_REFLINK                         RT_BIT_64(1)
/** Inode COW extent size hint is valid. */
#define XFS_INODE_F2_COWEXTSIZEHINT                  RT_BIT_64(2)
/** @} */


/**
 * Inode B-Tree record.
 */
typedef struct XFSINODEBTREEREC
{
    /** 0x00: Starting inode number. */
    uint32_t            uInodeStart;
    /** 0x04: Version dependent data. */
    union
    {
        /** Full (old) version. */
        struct
        {
            /** 0x04: Number of free inodes. */
            uint32_t    cInodesFree;
        } Full;
        /** Sparse (new) version. */
        struct
        {
            /** 0x04: Hole mask for sparse chunks. */
            uint16_t    bmHoles;
            /** 0x06: Total number of inodes. */
            uint8_t     cInodes;
            /** 0x07: Number of free inodes. */
            uint8_t     cInodesFree;
        } Sparse;
    } u;
    /** 0x08: Free inode mask. */
    uint64_t            bmInodesFree;
} XFSINODEBTREEREC;
/** Pointer to an inode B-Tree record. */
typedef XFSINODEBTREEREC *PXFSINODEBTREEREC;
/** Pointer to a const inode B-Tree record. */
typedef const XFSINODEBTREEREC *PCXFSINODEBTREEREC;


/**
 * XFS B+Tree root header.
 */
typedef struct XFSBTREEROOTHDR
{
    /** 0x00: Tree level. */
    uint16_t            iLvl;
    /** 0x02: Number of records. */
    uint16_t            cRecs;
} XFSBTREEROOTHDR;
/** Pointer to a B+Tree root header */
typedef XFSBTREEROOTHDR *PXFSBTREEROOTHDR;
/** Pointer to a const B+Tree root header. */
typedef const XFSBTREEROOTHDR *PCXFSBTREEROOTHDR;


/**
 * XFS B+Tree intermediate/leave node header.
 */
typedef struct XFSBTREENODEHDR
{
    /** 0x00: Magic identifying the node. */
    uint32_t            u32Magic;
    /** 0x04: Tree level. */
    uint16_t            iLvl;
    /** 0x06: Number of records. */
    uint16_t            cRecs;
    /** 0x08: Block number of the left sibling. */
    uint64_t            uSibLeft;
    /** 0x10: Block number of the right sibling. */
    uint64_t            uSibRight;
} XFSBTREENODEHDR;
/** Pointer to a B+Tree intermediate/leave node header. */
typedef XFSBTREENODEHDR *PXFSBTREENODEHDR;
/** Pointer to a const B+Tree intermediate/leave node header. */
typedef const XFSBTREENODEHDR *PCXFSBTREENODEHDR;

/** @name XFS_BTREENODEHDR_XXX - B+Tree node related defines.
 * @{ */
/** Magic for the tree node header. */
#define XFS_BTREENODEHDR_MAGIC                       RT_MAKE_U32_FROM_U8('P', 'A', 'M', 'B')
/** @} */


/**
 * XFS Extent.
 */
typedef struct XFSEXTENT
{
    /** 0x00: Low 64 bits. */
    uint64_t    u64Low;
    /** 0x08: High 64 bits. */
    uint64_t    u64High;
} XFSEXTENT;
/** Pointer to an XFS extent. */
typedef XFSEXTENT *PXFSEXTENT;
/** Pointer to a const XFS extent. */
typedef const XFSEXTENT *PCXFSEXTENT;

/** @name XFS_EXTENT_XXX - Extent related getters.
 * @{ */
/** Returns whether the extent is allocated but unwritten (true) or a normal extent (false). */
#define XFS_EXTENT_IS_UNWRITTEN(a_pExtent) (RT_BOOL((a_pExtent)->u64High & RT_BIT_64(63)))
/** Returns the number of blocks the extent covers. */
#define XFS_EXTENT_GET_BLOCK_COUNT(a_pExtent) ((a_pExtent)->u64Low & UINT64_C(0x1fffff))
/** Returns the absolute block number where the data is stored on the disk. */
#define XFS_EXTENT_GET_DISK_BLOCK(a_pExtent) (  (((a_pExtent)->u64High & UINT64_C(0x1ff)) << 42) \
                                              | (((a_pExtent)->u64Low & UINT64_C(0xffffffffffe00000)) >> 21))
/** Returns the logical inode block offset. */
#define XFS_EXTENT_GET_LOGICAL_BLOCK(a_pExtent) (((a_pExtent)->u64High & UINT64_C(0x7ffffffffffffe00)) >> 9)
/** @} */

/** @} */

#endif /* !IPRT_INCLUDED_formats_xfs_h */

