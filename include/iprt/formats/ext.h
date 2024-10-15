/* $Id: ext.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, Ext2/3/4 format.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_ext_h
#define IPRT_INCLUDED_formats_ext_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_ext    Extended Filesystem (EXT2/3/4) structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */

/*
 * The filesystem structures were retrieved from:
 * https://www.kernel.org/doc/html/latest/filesystems/ext4/index.html
 */

/** Offset where to find the first superblock on the disk, this is constant. */
#define EXT_SB_OFFSET               1024

/** @name EXT_INODE_NR_XXX - Special inode numbers.
 * @{ */
#define EXT_INODE_NR_DEF_BLOCKS     1   /**< List of defective blocks. */
#define EXT_INODE_NR_ROOT_DIR       2   /**< Root directory. */
#define EXT_INODE_NR_USER_QUOTA     3   /**< User quota. */
#define EXT_INODE_NR_GROUP_QUOTA    4   /**< Group quota. */
#define EXT_INODE_NR_BOOT_LOADER    5   /**< Boot loader. */
#define EXT_INODE_NR_UNDEL_DIR      6   /**< Undelete directory. */
#define EXT_INODE_NR_RESV_GRP_DESC  7   /**< Reserved group descriptors inode. */
#define EXT_INODE_NR_JOURNAL        8   /**< Journal. */
#define EXT_INODE_NR_EXCLUDE        9   /**< Exclude inode. */
#define EXT_INODE_NR_REPLICA       10   /**< Replica inode. */
/** @} */

/**
 * Ext superblock.
 *
 * Everything is stored little endian on the disk.
 */
typedef struct EXTSUPERBLOCK
{
    /** 0x00: Total number of inodes in the filesystem. */
    uint32_t    cInodesTotal;
    /** 0x04: Total number of blocks in the filesystem (low 32bits). */
    uint32_t    cBlocksTotalLow;
    /** 0x08: Number of blocks reserved for the super user (low 32bits). */
    uint32_t    cBlocksRsvdForSuperUserLow;
    /** 0x0c: Total number of free blocks (low 32bits). */
    uint32_t    cBlocksFreeLow;
    /** 0x10: Total number of free inodes. */
    uint32_t    cInodesFree;
    /** 0x14: First data block. */
    uint32_t    iBlockOfSuperblock;
    /** 0x18: Block size (calculated as 2^(10 + cBitsShiftLeftBlockSize)). */
    uint32_t    cLogBlockSize;
    /** 0x1c: Cluster size (calculated as 2^cLogClusterSize). */
    uint32_t    cLogClusterSize;
    /** 0x20: Number of blocks in each block group. */
    uint32_t    cBlocksPerGroup;
    /** 0x24: Number of clusters in each block group. */
    uint32_t    cClustersPerBlockGroup;
    /** 0x28: Number of inodes for each block group. */
    uint32_t    cInodesPerBlockGroup;
    /** 0x2c: Last mount time in seconds since epoch. */
    uint32_t    u32LastMountTime;
    /** 0x30: Last written time in seconds since epoch. */
    uint32_t    u32LastWrittenTime;
    /** 0x34: Number of times the volume was mounted since the last check. */
    uint16_t    cMountsSinceLastCheck;
    /** 0x36: Number of mounts allowed before a consistency check. */
    uint16_t    cMaxMountsUntilCheck;
    /** 0x38: Signature to identify a ext2 volume (EXT_SIGNATURE). */
    uint16_t    u16Signature;
    /** 0x3a: State of the filesystem (EXT_SB_STATE_XXX) */
    uint16_t    u16FilesystemState;
    /** 0x3c: What to do on an error. */
    uint16_t    u16ActionOnError;
    /** 0x3e: Minor revision level. */
    uint16_t    u16RevLvlMinor;
    /** 0x40: Time of last check in seconds since epoch. */
    uint32_t    u32LastCheckTime;
    /** 0x44: Interval between consistency checks in seconds. */
    uint32_t    u32CheckInterval;
    /** 0x48: Operating system ID of the filesystem creator (EXT_SB_OS_ID_CREATOR_XXX). */
    uint32_t    u32OsIdCreator;
    /** 0x4c: Revision level (EXT_SB_REV_XXX). */
    uint32_t    u32RevLvl;
    /** 0x50: User ID that is allowed to use reserved blocks. */
    uint16_t    u16UidReservedBlocks;
    /** 0x52: Group ID that is allowed to use reserved blocks. */
    uint16_t    u16GidReservedBlocks;
    /** 0x54: First non reserved inode number. */
    uint32_t    iFirstInodeNonRsvd;
    /** 0x58: Size of the inode structure in bytes. */
    uint16_t    cbInode;
    /** 0x5a: Block group number of this super block. */
    uint16_t    iBlkGrpSb;
    /** 0x5c: Compatible feature set flags (EXT_SB_FEAT_COMPAT_XXX). */
    uint32_t    fFeaturesCompat;
    /** 0x60: Incompatible feature set (EXT_SB_FEAT_INCOMPAT_XXX). */
    uint32_t    fFeaturesIncompat;
    /** 0x64: Readonly-compatible feature set (EXT_SB_FEAT_COMPAT_RO_XXX). */
    uint32_t    fFeaturesCompatRo;
    /** 0x68: 128bit UUID for the volume. */
    uint8_t     au8Uuid[16];
    /** 0x78: Volume name. */
    char        achVolumeName[16];
    /** 0x88: Directory were the filesystem was mounted last. */
    char        achLastMounted[64];
    /** 0xc8: Bitmap usage algorithm (used for compression). */
    uint32_t    u32AlgoUsageBitmap;
    /** 0xcc: Number of blocks to try to preallocate for files(?). */
    uint8_t     cBlocksPrealloc;
    /** 0xcd: Number of blocks to try to preallocate for directories. */
    uint8_t     cBlocksPreallocDirectory;
    /** 0xce: Number of reserved group descriptor entries for future filesystem expansion. */
    uint16_t    cGdtEntriesRsvd;
    /** 0xd0: 128bit UUID for the journal superblock. */
    uint8_t     au8JournalUuid[16];
    /** 0xe0: Inode number of the journal file. */
    uint32_t    iJournalInode;
    /** 0xe4: Device number of journal file (if the appropriate feature flag is set). */
    uint32_t    u32JournalDev;
    /** 0xe8: Start of list of orpaned inodes to delete. */
    uint32_t    u32LastOrphan;
    /** 0xec: HTREE hash seed. */
    uint32_t    au32HashSeedHtree[4];
    /** 0xfc: Default hash algorithm to use for hashes (EXT_SB_HASH_VERSION_DEF_XXX). */
    uint8_t     u8HashVersionDef;
    /** 0xfd: Journal backup type. */
    uint8_t     u8JnlBackupType;
    /** 0xfe: Group descriptor size in bytes. */
    uint16_t    cbGroupDesc;
    /** 0x100: Default mount options (EXT_SB_MNT_OPTS_DEF_XXX). */
    uint32_t    fMntOptsDef;
    /** 0x104: First metablock block group (if feature is enabled). */
    uint32_t    iFirstMetaBg;
    /** 0x108: Filesystem creation time in seconds since epoch. */
    uint32_t    u32TimeFsCreation;
    /** 0x10c: Backup copy of journals inodes block array for the first elements. */
    uint32_t    au32JnlBlocks[17];
    /** 0x150: Total number of blocks in the filesystem (high 32bits). */
    uint32_t    cBlocksTotalHigh;
    /** 0x154: Number of blocks reserved for the super user (high 32bits). */
    uint32_t    cBlocksRsvdForSuperUserHigh;
    /** 0x158: Total number of free blocks (high 32bits). */
    uint32_t    cBlocksFreeHigh;
    /** 0x15c: All inodes have at least this number of bytes. */
    uint16_t    cbInodesExtraMin;
    /** 0x15e: New inodes should reserve this number of bytes. */
    uint16_t    cbNewInodesRsv;
    /** 0x160: Miscellaneous flags (EXT_SB_F_XXX). */
    uint32_t    fFlags;
    /** 0x164: RAID stride, number of logical blocks read from or written to the disk
     * before moving to the next disk. */
    uint16_t    cRaidStride;
    /** 0x166: Number of seconds between multi-mount prevention checking. */
    uint16_t    cSecMmpInterval;
    /** 0x168: Block number for the multi-mount protection data. */
    uint64_t    iMmpBlock;
    /** 0x170: Raid stride width. */
    uint32_t    cRaidStrideWidth;
    /** 0x174: Size of a flexible block group (calculated as 2^cLogGroupsPerFlex). */
    uint8_t     cLogGroupsPerFlex;
    /** 0x175: Metadata checksum algorithm type, only 1 is valid (for CRC32c). */
    uint8_t     u8ChksumType;
    /** 0x176: Padding. */
    uint16_t    u16Padding;
    /** 0x178: Number of KiB written to the filesystem so far. */
    uint64_t    cKbWritten;
    /** 0x180: Inode number of active snapshot. */
    uint32_t    iSnapshotInode;
    /** 0x184: Sequential ID of active snapshot. */
    uint32_t    iSnapshotId;
    /** 0x188: Number of blocks reserved for activ snapshot's future use. */
    uint64_t    cSnapshotRsvdBlocks;
    /** 0x190: Inode number of the head of the on-disk snapshot list. */
    uint32_t    iSnapshotListInode;
    /** 0x194: Number of errors seen so far. */
    uint32_t    cErrorsSeen;
    /** 0x198: First time an error happened in seconds since epoch. */
    uint32_t    u32TimeFirstError;
    /** 0x19c: Inode involved in the first error. */
    uint32_t    iInodeFirstError;
    /** 0x1a0: Number of block involved of first error. */
    uint64_t    iBlkFirstError;
    /** 0x1a8: Name of the function where the first error happened. */
    char        achFuncFirstError[32];
    /** 0x1c8: Line number where the error happened. */
    uint32_t    iLineFirstError;
    /** 0x1cc: Time of the most receent error in seconds since epoch. */
    uint32_t    u32TimeLastError;
    /** 0x1d0: Inode involved in the most recent error. */
    uint32_t    iInodeLastError;
    /** 0x1d4: Line number where the most recent error happened. */
    uint32_t    iLineLastError;
    /** 0x1d8: Number of block involved of most recent error. */
    uint64_t    iBlkLastError;
    /** 0x1e0: Name of the function where the most recent error happened. */
    char        achFuncLastError[32];
    /** 0x200: ASCIIz string of mount options. */
    char        aszMntOpts[64];
    /** 0x240: Inode number of user quota file. */
    uint32_t    iInodeUsrQuota;
    /** 0x244: Inode number of group quota file. */
    uint32_t    iInodeGrpQuota;
    /** 0x248: Overhead blocks/clusters in filesystem. */
    uint32_t    cOverheadBlocks;
    /** 0x24c: Block groups containing superblock backups. */
    uint32_t    aiBlkGrpSbBackups[2];
    /** 0x254: Encryption algorithms in use (EXT_SB_ENCRYPT_ALGO_XXX). */
    uint8_t     au8EncryptAlgo[4];
    /** 0x258: Salt for the string2key algorithm for encryption. */
    uint8_t     abEncryptPwSalt[16];
    /** 0x268: Inode number of lost+found. */
    uint32_t    iInodeLostFound;
    /** 0x26c: Inode that tracks project quotas. */
    uint32_t    iInodeProjQuota;
    /** 0x270: Checksum seed used for the metadata checksum calculations.
     * Should be crc32c(~0, au8Uuid). */
    uint32_t    u32ChksumSeed;
    /** 0x274: Upper 8bits of the u32LastWrittenTime field. */
    uint8_t     u32LastWrittenTimeHigh8Bits;
    /** 0x275: Upper 8bits of the u32LastMountTime field. */
    uint8_t     u32LastMountTimeHigh8Bits;
    /** 0x276: Upper 8bits of the u32TimeFsCreation field. */
    uint8_t     u32TimeFsCreationHigh8Bits;
    /** 0x277: Upper 8bits of the u32LastCheckTime field. */
    uint8_t     u32LastCheckTimeHigh8Bits;
    /** 0x278: Upper 8bits of the u32TimeFirstError field. */
    uint8_t     u32TimeFirstErrorHigh8Bits;
    /** 0x279: Upper 8bits of the u32TimeLastError field. */
    uint8_t     u32TimeLastErrorHigh8Bits;
    /** 0x27a: Zero padding. */
    uint8_t     au8Padding[2];
    /** 0x27c: Padding to the end of the block. */
    uint32_t    au32Rsvd[96];
    /** 0x3fc: Superblock checksum. */
    uint32_t    u32Chksum;
} EXTSUPERBLOCK;
AssertCompileMemberOffset(EXTSUPERBLOCK, u16UidReservedBlocks, 0x50);
AssertCompileMemberOffset(EXTSUPERBLOCK, u32AlgoUsageBitmap, 0xc8);
AssertCompileMemberOffset(EXTSUPERBLOCK, iJournalInode, 0xe0);
AssertCompileMemberOffset(EXTSUPERBLOCK, u8HashVersionDef, 0xfc);
AssertCompileMemberOffset(EXTSUPERBLOCK, fMntOptsDef, 0x100);
AssertCompileMemberOffset(EXTSUPERBLOCK, iBlkLastError, 0x1d8);
AssertCompileMemberOffset(EXTSUPERBLOCK, iInodeLostFound, 0x268);
AssertCompileSize(EXTSUPERBLOCK, 1024);
/** Pointer to an ext super block. */
typedef EXTSUPERBLOCK *PEXTSUPERBLOCK;
/** Pointer to a const ext super block. */
typedef EXTSUPERBLOCK const *PCEXTSUPERBLOCK;

/** Ext signature. */
#define EXT_SB_SIGNATURE                      UINT16_C(0xef53)

/** @name EXT_SB_STATE_XXX - Filesystem state
 * @{ */
/** Clean filesystem state. */
#define EXT_SB_STATE_CLEAN                    UINT16_C(0x0001)
/** Error filesystem state. */
#define EXT_SB_STATE_ERRORS                   UINT16_C(0x0002)
/** Orphans being recovered state. */
#define EXT_SB_STATE_ORPHANS_RECOVERING       UINT16_C(0x0004)
/** @} */

/** @name EXT_SB_OS_ID_CREATOR_XXX - Filesystem creator
 * @{ */
/** Linux. */
#define EXT_SB_OS_ID_CREATOR_LINUX            0
/** Hurd. */
#define EXT_SB_OS_ID_CREATOR_HURD             1
/** Masix. */
#define EXT_SB_OS_ID_CREATOR_MASIX            2
/** FreeBSD. */
#define EXT_SB_OS_ID_CREATOR_FREEBSD          3
/** Lites. */
#define EXT_SB_OS_ID_CREATOR_LITES            4
/** @} */

/** @name EXT_SB_REV_XXX - Superblock revision
 * @{ */
/** Original format (ext2). */
#define EXT_SB_REV_ORIG                       0
/** Inodes have dynmic sizes. */
#define EXT_SB_REV_V2_DYN_INODE_SZ            1
/** @} */

/** @name EXT_SB_FEAT_COMPAT_XXX - Compatible features which can be ignored when set
 * and not being supported.
 * @{ */
/** Directories can be preallocated. */
#define EXT_SB_FEAT_COMPAT_DIR_PREALLOC       RT_BIT_32(0)
/** Some sort of "imagic" inodes. */
#define EXT_SB_FEAT_COMPAT_IMAGIC_INODES      RT_BIT_32(1)
/** Filesystem has a journal. */
#define EXT_SB_FEAT_COMPAT_HAS_JOURNAL        RT_BIT_32(2)
/** Filesystem supports extended attributes. */
#define EXT_SB_FEAT_COMPAT_EXT_ATTR           RT_BIT_32(3)
/** Filesystem contains reserved group descriptor blocks for filesystem expansion. */
#define EXT_SB_FEAT_COMPAT_RESIZE_INODE       RT_BIT_32(4)
/** Filesystem contains directory indices. */
#define EXT_SB_FEAT_COMPAT_DIR_INDEX          RT_BIT_32(5)
/** Lazy block group - not used. */
#define EXT_SB_FEAT_COMPAT_LAZY_BG            RT_BIT_32(6)
/** Exclude inode - not used. */
#define EXT_SB_FEAT_COMPAT_EXCLUDE_INODE      RT_BIT_32(7)
/** Exclude bitmap - not used. */
#define EXT_SB_FEAT_COMPAT_EXCLUDE_BITMAP     RT_BIT_32(8)
/** Sparse super blocks, super block contains pointers to block groups
 * containing backups of the superblock. */
#define EXT_SB_FEAT_COMPAT_SPARSE_SUPER2      RT_BIT_32(9)
/** @} */

/** @name EXT_SB_FEAT_INCOMPAT_XXX - Incompatible features which cause a mounting
 * error when set and not being supported.
 * @{ */
/** Filesystem contains compressed files. */
#define EXT_SB_FEAT_INCOMPAT_COMPRESSION      RT_BIT_32(0)
/** Directory entries contain a file type. */
#define EXT_SB_FEAT_INCOMPAT_DIR_FILETYPE     RT_BIT_32(1)
/** Filesystem needs recovery. */
#define EXT_SB_FEAT_INCOMPAT_RECOVER          RT_BIT_32(2)
/** The journal is recorded on a separate device. */
#define EXT_SB_FEAT_INCOMPAT_JOURNAL_DEV      RT_BIT_32(3)
/** Filesystem uses meta block groups. */
#define EXT_SB_FEAT_INCOMPAT_META_BG          RT_BIT_32(4)
/** Files in the filesystem use extents. */
#define EXT_SB_FEAT_INCOMPAT_EXTENTS          RT_BIT_32(6)
/** Filesystem uses 64bit offsets. */
#define EXT_SB_FEAT_INCOMPAT_64BIT            RT_BIT_32(7)
/** Filesystem requires multiple mount preotection. */
#define EXT_SB_FEAT_INCOMPAT_MMP              RT_BIT_32(8)
/** Filesystem uses flexible block groups. */
#define EXT_SB_FEAT_INCOMPAT_FLEX_BG          RT_BIT_32(9)
/** Inodes can be used to store large extended attribute values. */
#define EXT_SB_FEAT_INCOMPAT_EXT_ATTR_INODE   RT_BIT_32(10)
/** Data is contained in directory entries. */
#define EXT_SB_FEAT_INCOMPAT_DIRDATA          RT_BIT_32(12)
/** Metadata checksum seed is stored in the super block. */
#define EXT_SB_FEAT_INCOMPAT_CSUM_SEED        RT_BIT_32(13)
/** Directories can be larger than 2GiB or contain a 3-level HTree. */
#define EXT_SB_FEAT_INCOMPAT_LARGE_DIR        RT_BIT_32(14)
/** Data is inlined in the inode. */
#define EXT_SB_FEAT_INCOMPAT_INLINE_DATA      RT_BIT_32(15)
/** Encrypted inodes are present on the filesystem. */
#define EXT_SB_FEAT_INCOMPAT_ENCRYPT          RT_BIT_32(16)
/** @} */

/** @name EXT_SB_FEAT_COMPAT_RO_XXX - Backward compatible features when mounted readonly
 * @{ */
/** Sparse superblocks. */
#define EXT_SB_FEAT_COMPAT_RO_SPARSE_SUPER    RT_BIT_32(0)
/** There is at least one large file (> 2GiB). */
#define EXT_SB_FEAT_COMPAT_RO_LARGE_FILE      RT_BIT_32(1)
/** Actually not used in the Linux kernel and e2fprogs. */
#define EXT_SB_FEAT_COMPAT_RO_BTREE_DIR       RT_BIT_32(2)
/** Filesystem contains files which sizes are not represented as a multiple of 512 byte sectors
 * but logical blocks instead. */
#define EXT_SB_FEAT_COMPAT_RO_HUGE_FILE       RT_BIT_32(3)
/** Group descriptors have checksums embedded */
#define EXT_SB_FEAT_COMPAT_RO_GDT_CHSKUM      RT_BIT_32(4)
/** Subdirectory limit of 32000 doesn't apply. The link count is set to 1 if beyond 64999. */
#define EXT_SB_FEAT_COMPAT_RO_DIR_NLINK       RT_BIT_32(5)
/** Inodes can contain extra data. */
#define EXT_SB_FEAT_COMPAT_RO_EXTRA_INODE_SZ  RT_BIT_32(6)
/** There is at least one snapshot on the filesystem. */
#define EXT_SB_FEAT_COMPAT_RO_HAS_SNAPSHOTS   RT_BIT_32(7)
/** Quotas are enabled for this filesystem. */
#define EXT_SB_FEAT_COMPAT_RO_QUOTA           RT_BIT_32(8)
/** The bigalloc feature is enabled, file extents are tracked in units of clusters
 * instead of blocks. */
#define EXT_SB_FEAT_COMPAT_RO_BIGALLOC        RT_BIT_32(9)
/** Metadata contains checksums. */
#define EXT_SB_FEAT_COMPAT_RO_METADATA_CHKSUM RT_BIT_32(10)
/** Filesystem supports replicas. */
#define EXT_SB_FEAT_COMPAT_RO_REPLICA         RT_BIT_32(11)
/** Filesystem is readonly. */
#define EXT_SB_FEAT_COMPAT_RO_READONLY        RT_BIT_32(12)
/** Filesystem tracks project quotas. */
#define EXT_SB_FEAT_COMPAT_RO_PROJECT         RT_BIT_32(13)
/** @} */

/** @name EXT_SB_HASH_VERSION_DEF_XXX - Default hash algorithm used
 * @{ */
/** Legacy. */
#define EXT_SB_HASH_VERSION_DEF_LEGACY               0
/** Half MD4. */
#define EXT_SB_HASH_VERSION_DEF_HALF_MD4             1
/** Tea. */
#define EXT_SB_HASH_VERSION_DEF_TEA                  2
/** Unsigned legacy. */
#define EXT_SB_HASH_VERSION_DEF_LEGACY_UNSIGNED      3
/** Unsigned half MD4. */
#define EXT_SB_HASH_VERSION_DEF_HALF_MD4_UNSIGNED    4
/** Unsigned tea. */
#define EXT_SB_HASH_VERSION_DEF_TEA_UNSIGNED         5
/** @} */

/** @name EXT_SB_MNT_OPTS_DEF_XXX - Default mount options
 * @{ */
/** Print debugging information on (re)mount. */
#define EXT_SB_MNT_OPTS_DEF_DEBUG                    RT_BIT_32(0)
/** Created files take the group ID ofthe containing directory. */
#define EXT_SB_MNT_OPTS_DEF_BSDGROUPS                RT_BIT_32(1)
/** Support userspace extended attributes. */
#define EXT_SB_MNT_OPTS_DEF_XATTR_USER               RT_BIT_32(2)
/** Support POSIX access control lists. */
#define EXT_SB_MNT_OPTS_DEF_ACL                      RT_BIT_32(3)
/** Do not support 32bit UIDs. */
#define EXT_SB_MNT_OPTS_DEF_UID16                    RT_BIT_32(4)
/** All data and metadata are committed to the journal. */
#define EXT_SB_MNT_OPTS_DEF_JMODE_DATA               RT_BIT_32(5)
/** All data are flushed to the disk before metadata are committed to the journal. */
#define EXT_SB_MNT_OPTS_DEF_JMODE_ORDERED            RT_BIT_32(6)
/** Data ordering not preserved, data may be written after metadata has been written. */
#define EXT_SB_MNT_OPTS_DEF_JMODE_WBACK              (EXT_SB_MNT_OPTS_DEF_JMODE_DATA | EXT_SB_MNT_OPTS_DEF_JMODE_ORDERED)
/** No write flushes. */
#define EXT_SB_MNT_OPTS_DEF_NOBARRIER                RT_BIT_32(8)
/** Track metadata blocks on the filesystem not being used as data blocks. */
#define EXT_SB_MNT_OPTS_DEF_BLOCK_VALIDITY           RT_BIT_32(9)
/** Enables TRIM/DISCARD support. */
#define EXT_SB_MNT_OPTS_DEF_DISCARD                  RT_BIT_32(10)
/** Disable delayed allocation. */
#define EXT_SB_MNT_OPTS_DEF_NODELALLOC               RT_BIT_32(11)
/** @} */

/** @name EXT_SB_F_XXX - Superblock flags
 * @{ */
/** Signed directory hash used. */
#define EXT_SB_F_SIGNED_DIR_HASH                 RT_BIT_32(0)
/** Unsigned directory hash used. */
#define EXT_SB_F_UNSIGNED_DIR_HASH               RT_BIT_32(1)
/** Only used to test development code. */
#define EXT_SB_F_DEV_CODE                        RT_BIT_32(3)
/** @} */

/** @name EXT_SB_ENCRYPT_ALGO_XXX - Group descriptor flags
 * @{ */
/** Invalid encryption algorithm. */
#define EXT_SB_ENCRYPT_ALGO_INVALID                  0
/** 256-bit AES in XTS mode. */
#define EXT_SB_ENCRYPT_ALGO_256BIT_AES_XTS           1
/** 256-bit AES in GCM mode. */
#define EXT_SB_ENCRYPT_ALGO_256BIT_AES_GCM           2
/** 256-bit AES in CBC mode. */
#define EXT_SB_ENCRYPT_ALGO_256BIT_AES_CBC           3
/** @} */


/**
 * Block group descriptor (32byte version).
 */
typedef struct EXTBLOCKGROUPDESC32
{
    /** 0x00: Block address of the block bitmap (low 32bits). */
    uint32_t    offBlockBitmapLow;
    /** 0x04: Block address of the inode bitmap (low 32bits). */
    uint32_t    offInodeBitmapLow;
    /** 0x08: Start block address of the inode table (low 32bits). */
    uint32_t    offInodeTableLow;
    /** 0x0c: Number of unallocated blocks in group (low 16bits). */
    uint16_t    cBlocksFreeLow;
    /** 0x0e: Number of unallocated inodes in group (low 16bits). */
    uint16_t    cInodesFreeLow;
    /** 0x10: Number of directories in the group (low 16bits). */
    uint16_t    cDirectoriesLow;
    /** 0x12: Flags (EXT_GROUP_DESC_F_XXX). */
    uint16_t    fFlags;
    /** 0x14: Location of snapshot exclusion bitmap (lower 32bits) */
    uint32_t    offSnapshotExclBitmapLow;
    /** 0x18: Block bitmap checksum (lower 16bits). */
    uint16_t    u16ChksumBlockBitmapLow;
    /** 0x1a: Inode bitmap checksum (lower 16bits). */
    uint16_t    u16ChksumInodeBitmapLow;
    /** 0x1c: Unused inode entry count in the groups inode table (lower 16bits).*/
    uint16_t    cInodeTblUnusedLow;
    /** 0x1e: Group descriptor checksum. */
    uint16_t    u16Chksum;
} EXTBLOCKGROUPDESC32;
AssertCompileSize(EXTBLOCKGROUPDESC32, 32);
/** Pointer to an ext block group descriptor. */
typedef EXTBLOCKGROUPDESC32 *PEXTBLOCKGROUPDESC32;
/** Pointer to a const 32 byte block group descriptor. */
typedef const EXTBLOCKGROUPDESC32 *PCEXTBLOCKGROUPDESC32;


/**
 * Block group descriptor (64byte version).
 */
typedef struct EXTBLOCKGROUPDESC64
{
    /** 0x00: Embedded 32 byte descriptor. */
    EXTBLOCKGROUPDESC32    v32;
    /** 0x20: Location of block bitmap (upper 32bits). */
    uint32_t    offBlockBitmapHigh;
    /** 0x24: Location of inode bitmap (upper 32bits). */
    uint32_t    offInodeBitmapHigh;
    /** 0x28: Location of inode table (upper 32bits). */
    uint32_t    offInodeTableHigh;
    /** 0x2c: Number of unallocated blocks (upper 16bits). */
    uint16_t    cBlocksFreeHigh;
    /** 0x2e: Number of unallocated inodes (upper 16bits). */
    uint16_t    cInodesFreeHigh;
    /** 0x30: Number of directories in the group (upper 16bits). */
    uint16_t    cDirectoriesHigh;
    /** 0x32: Unused inode entry count in the groups inode table (upper 16bits).*/
    uint16_t    cInodeTblUnusedHigh;
    /** 0x34: Location of snapshot exclusion bitmap (upper 32bits) */
    uint32_t    offSnapshotExclBitmapHigh;
    /** 0x38: Block bitmap checksum (upper 16bits). */
    uint16_t    u16ChksumBlockBitmapHigh;
    /** 0x3a: Inode bitmap checksum (upper 16bits). */
    uint16_t    u16ChksumInodeBitmapHigh;
    /** 0x3c: Padding to 64 bytes. */
    uint32_t    u64Padding;
} EXTBLOCKGROUPDESC64;
AssertCompileSize(EXTBLOCKGROUPDESC64, 64);
/** Pointer to an ext block group descriptor. */
typedef EXTBLOCKGROUPDESC64 *PEXTBLOCKGROUPDESC64;
/** Pointer to a const 64 byte block group descriptor. */
typedef const EXTBLOCKGROUPDESC64 *PCEXTBLOCKGROUPDESC64;

/** @name EXT_GROUP_DESC_F_XXX - Group descriptor flags
 * @{ */
/** Inode table and bitmaps are not initialized. */
#define EXT_GROUP_DESC_F_INODE_UNINIT                RT_BIT(0)
/** Block bitmap is not initialized. */
#define EXT_GROUP_DESC_F_BLOCK_UNINIT                RT_BIT(1)
/** Inode table is zeroed. */
#define EXT_GROUP_DESC_F_INODE_ZEROED                RT_BIT(2)
/** @} */


/**
 * Combiend view of the different block gorup descriptor versions.
 */
typedef union EXTBLOCKGROUPDESC
{
    /** 32 byte version. */
    EXTBLOCKGROUPDESC32    v32;
    /** 64 byte version. */
    EXTBLOCKGROUPDESC64    v64;
    /** Byte view. */
    uint8_t               au8[64];
} EXTBLOCKGROUPDESC;
/** Poiner to a unified block gorup descriptor view. */
typedef EXTBLOCKGROUPDESC *PEXTBLOCKGROUPDESC;
/** Poiner to a const unified block gorup descriptor view. */
typedef const EXTBLOCKGROUPDESC *PCEXTBLOCKGROUPDESC;


/** Number of block entries in the inodes block map. */
#define EXT_INODE_BLOCK_ENTRIES                      15

/**
 * Inode table entry (standard 128 byte version).
 */
typedef struct EXTINODE
{
    /** 0x00: File mode (EXT_INODE_FILE_MODE_XXX). */
    uint16_t    fMode;
    /** 0x02: Owner UID (lower 16bits). */
    uint16_t    uUidLow;
    /** 0x04: Size in bytes (lower 32bits). */
    uint32_t    cbSizeLow;
    /** 0x08: Last access time in seconds since epoch. */
    uint32_t    u32TimeLastAccess;
    /** 0x0c: Last inode change time in seconds since epoch. */
    uint32_t    u32TimeLastChange;
    /** 0x10: Last data modification time in seconds since epoch. */
    uint32_t    u32TimeLastModification;
    /** 0x14: Deletion time in seconds since epoch. */
    uint32_t    u32TimeDeletion;
    /** 0x18: Group ID (lower 16bits). */
    uint16_t    uGidLow;
    /** 0x1a: Hard link count. */
    uint16_t    cHardLinks;
    /** 0x1c: Block count (lower 32bits). */
    uint32_t    cBlocksLow;
    /** 0x20: Inode flags. */
    uint32_t    fFlags;
    /** 0x24: Operating system dependent data. */
    union
    {
        /** Linux: Inode version. */
        uint32_t    u32LnxVersion;
    } Osd1;
    /** 0x28: Block map or extent tree. */
    uint32_t    au32Block[EXT_INODE_BLOCK_ENTRIES];
    /** 0x64: File version. */
    uint32_t    u32Version;
    /** 0x68: Extended attribute control block (lower 32bits). */
    uint32_t    offExtAttrLow;
    /** 0x6c: File/directory size (upper 32bits). */
    uint32_t    cbSizeHigh;
    /** 0x70: Fragment address (obsolete). */
    uint32_t    u32FragmentAddrObs;
    /** 0x74: Operating system dependent data 2. */
    union
    {
        /** Linux related data. */
        struct
        {
            /** 0x00: Block count (upper 16bits). */
            uint16_t    cBlocksHigh;
            /** 0x02: Extended attribute block location (upper 16bits). */
            uint16_t    offExtAttrHigh;
            /** 0x04: Owner UID (upper 16bits). */
            uint16_t    uUidHigh;
            /** 0x06: Group ID (upper 16bits). */
            uint16_t    uGidHigh;
            /** 0x08: Inode checksum (lower 16bits). */
            uint16_t    u16ChksumLow;
            /** 0x0a: Reserved */
            uint16_t    u16Rsvd;
        } Lnx;
    } Osd2;
} EXTINODE;
AssertCompileSize(EXTINODE, 128);
/** Pointer to an inode. */
typedef EXTINODE *PEXTINODE;
/** Pointer to a const inode. */
typedef const EXTINODE *PCEXTINODE;


/**
 * Extra inode data (coming right behind the fixed inode data).
 */
typedef struct EXTINODEEXTRA
{
    /** 0x80: Size of the extra inode data in bytes. */
    uint16_t    cbInodeExtra;
    /** 0x82: Inode checksum (upper 16bits.) */
    uint16_t    u16ChksumHigh;
    /** 0x84: Last inode change time, extra time bits for sub-second precision. */
    uint32_t    u32ExtraTimeLastChange;
    /** 0x88: Last data modification time, extra time bits for sub-second precision. */
    uint32_t    u32ExtraTimeLastModification;
    /** 0x8c: Last access time, extra time bits for sub-second precision. */
    uint32_t    u32ExtraTimeLastAccess;
    /** 0x90: File creation time in seconds since epoch. */
    uint32_t    u32TimeCreation;
    /** 0x94: File creation time, extra time bits for sub-second precision. */
    uint32_t    u32ExtraTimeCreation;
    /** 0x98: Version number (upper 32bits). */
    uint32_t    u32VersionHigh;
    /** 0x9c: Project ID. */
    uint32_t    u32ProjectId;
} EXTINODEEXTRA;
/** Pointer to extra inode data. */
typedef EXTINODEEXTRA *PEXTINODEEXTRA;
/** Pointer to a const extra inode data. */
typedef const EXTINODEEXTRA *PCEXTINODEEXTRA;


/**
 * Combined inode data.
 */
typedef struct EXTINODECOMB
{
    /** Core inode structure. */
    EXTINODE      Core;
    /** Any extra inode data which might be present. */
    EXTINODEEXTRA Extra;
} EXTINODECOMB;
/** Pointer to combined inode data. */
typedef EXTINODECOMB *PEXTINODECOMB;
/** Pointer to a const combined inode data. */
typedef const EXTINODECOMB *PCEXTINODECOMB;



/** @name EXT_INODE_MODE_XXX - File mode
 * @{ */
/** Others can execute the file. */
#define EXT_INODE_MODE_EXEC_OTHER                    RT_BIT(0)
/** Others can write to the file. */
#define EXT_INODE_MODE_WRITE_OTHER                   RT_BIT(1)
/** Others can read the file. */
#define EXT_INODE_MODE_READ_OTHER                    RT_BIT(2)
/** Members of the same group can execute the file. */
#define EXT_INODE_MODE_EXEC_GROUP                    RT_BIT(3)
/** Members of the same group can write to the file. */
#define EXT_INODE_MODE_WRITE_GROUP                   RT_BIT(4)
/** Members of the same group can read the file. */
#define EXT_INODE_MODE_READ_GROUP                    RT_BIT(5)
/** Owner can execute the file. */
#define EXT_INODE_MODE_EXEC_OWNER                    RT_BIT(6)
/** Owner can write to the file. */
#define EXT_INODE_MODE_WRITE_OWNER                   RT_BIT(7)
/** Owner can read the file. */
#define EXT_INODE_MODE_READ_OWNER                    RT_BIT(8)
/** Sticky file mode. */
#define EXT_INODE_MODE_STICKY                        RT_BIT(9)
/** File is set GID. */
#define EXT_INODE_MODE_SET_GROUP_ID                  RT_BIT(10)
/** File is set UID. */
#define EXT_INODE_MODE_SET_USER_ID                   RT_BIT(11)
/** @} */

/** @name EXT_INODE_MODE_TYPE_XXX - File type
 * @{ */
/** Inode represents a FIFO. */
#define EXT_INODE_MODE_TYPE_FIFO                     UINT16_C(0x1000)
/** Inode represents a character device. */
#define EXT_INODE_MODE_TYPE_CHAR                     UINT16_C(0x2000)
/** Inode represents a directory. */
#define EXT_INODE_MODE_TYPE_DIR                      UINT16_C(0x4000)
/** Inode represents a block device. */
#define EXT_INODE_MODE_TYPE_BLOCK                    UINT16_C(0x6000)
/** Inode represents a regular file. */
#define EXT_INODE_MODE_TYPE_REGULAR                  UINT16_C(0x8000)
/** Inode represents a symlink. */
#define EXT_INODE_MODE_TYPE_SYMLINK                  UINT16_C(0xa000)
/** Inode represents a socket. */
#define EXT_INODE_MODE_TYPE_SOCKET                   UINT16_C(0xc000)
/** Returns the inode type from the combined mode field. */
#define EXT_INODE_MODE_TYPE_GET_TYPE(a_Mode)         ((a_Mode) & 0xf000)
/** @} */

/** @name EXT_INODE_F_XXX - Inode flags
 * @{ */
/** Inode requires secure erase on deletion. */
#define EXT_INODE_F_SECURE_ERASE                     RT_BIT_32(0)
/** Inode should be preserved for undeletion during deletion. */
#define EXT_INODE_F_UNDELETE                         RT_BIT_32(1)
/** Inode contains compressed data. */
#define EXT_INODE_F_COMPRESSED                       RT_BIT_32(2)
/** All writes to this inode must be synchronous. */
#define EXT_INODE_F_SYNCHRONOUS                      RT_BIT_32(3)
/** Inode is immutable. */
#define EXT_INODE_F_IMMUTABLE                        RT_BIT_32(4)
/** Inode is append only. */
#define EXT_INODE_F_APPEND_ONLY                      RT_BIT_32(5)
/** Inode should not be dumped via dump(1). */
#define EXT_INODE_F_NO_DUMP                          RT_BIT_32(6)
/** Access time is not updated. */
#define EXT_INODE_F_NO_ACCESS_TIME                   RT_BIT_32(7)
/** Dirty compressed file. */
#define EXT_INODE_F_DIRTY_COMPRESSED                 RT_BIT_32(8)
/** Inode has one or more compressed clusters. */
#define EXT_INODE_F_COMPRESSED_BLOCK                 RT_BIT_32(9)
/** Inode should not be compressed. */
#define EXT_INODE_F_NO_COMPRESSION                   RT_BIT_32(10)
/** Inode is encrypted. */
#define EXT_INODE_F_ENCRYPTED                        RT_BIT_32(11)
/** Directory has hashed indexes. */
#define EXT_INODE_F_DIR_HASHED_INDEX                 RT_BIT_32(12)
/** AFS magic directory. */
#define EXT_INODE_F_IMAGIC                           RT_BIT_32(13)
/** Data must always be written through the journal. */
#define EXT_INODE_F_JOURNAL_DATA                     RT_BIT_32(14)
/** File tail should not be merged. */
#define EXT_INODE_F_NOTAIL                           RT_BIT_32(15)
/** All directory entry data should be written synchronously. */
#define EXT_INODE_F_DIR_SYNCHRONOUS                  RT_BIT_32(16)
/** Top of directory hierarchy. */
#define EXT_INODE_F_TOP_DIRECTORY                    RT_BIT_32(17)
/** Inode is a huge file. */
#define EXT_INODE_F_HUGE_FILE                        RT_BIT_32(18)
/** Inode uses extents. */
#define EXT_INODE_F_EXTENTS                          RT_BIT_32(19)
/** Inode stores a large extended attribute value in its data blocks. */
#define EXT_INODE_F_EXT_ATTR_INODE                   RT_BIT_32(20)
/** File has blocks allocated past end of file. */
#define EXT_INODE_F_ALLOC_BLOCKS_EOF                 RT_BIT_32(21)
/** Inode is a snapshot. */
#define EXT_INODE_F_SNAPSHOT                         RT_BIT_32(22)
/** Snapshot is being deleted. */
#define EXT_INODE_F_SNAPSHOT_DELETED                 RT_BIT_32(23)
/** Snapshot shrink has completed. */
#define EXT_INODE_F_SNAPSHOT_SHRUNK                  RT_BIT_32(24)
/** Inode contains inline data. */
#define EXT_INODE_F_INLINE_DATA                      RT_BIT_32(25)
/** Children are created with the same project ID. */
#define EXT_INODE_F_PROJECT_ID_INHERIT               RT_BIT_32(26)
/** Reserved for ext4 library. */
#define EXT_INODE_F_RESERVED_LIBRARY                 RT_BIT_32(27)
/** @} */


/**
 * Extent tree header.
 */
typedef struct EXTEXTENTHDR
{
    /** 0x00: Magic number for identification. */
    uint16_t    u16Magic;
    /** 0x02: Number of valid entries following. */
    uint16_t    cEntries;
    /** 0x04: Maxmimum number of entries that could follow. */
    uint16_t    cMax;
    /** 0x06: Depth of this extent node in the tree. */
    uint16_t    uDepth;
    /** 0x08: Generation of the tree (not used by standard ext4). */
    uint32_t    cGeneration;
} EXTEXTENTHDR;
AssertCompileSize(EXTEXTENTHDR, 12);
/** Pointer to a extent tree header. */
typedef EXTEXTENTHDR *PEXTEXTENTHDR;
/** Pointer to a const extent tree header. */
typedef const EXTEXTENTHDR *PCEXTEXTENTHDR;

/** Magic number identifying an extent header. */
#define EXT_EXTENT_HDR_MAGIC                         UINT16_C(0xf30a)
/** Maximum depth an extent header can have. */
#define EXT_EXTENT_HDR_DEPTH_MAX                     UINT16_C(5)


/**
 * Extent tree index node.
 */
typedef struct EXTEXTENTIDX
{
    /** 0x00: Start file block  this node covers. */
    uint32_t    iBlock;
    /** 0x04: Block number of child extent node (lower 32bits). */
    uint32_t    offChildLow;
    /** 0x08: Block number of child extent node (upper 16bits). */
    uint16_t    offChildHigh;
    /** 0x0a: Reserved. */
    uint16_t    u16Rsvd;
} EXTEXTENTIDX;
AssertCompileSize(EXTEXTENTIDX, 12);
/** Pointer to an extent tree index node. */
typedef EXTEXTENTIDX *PEXTEXTENTIDX;
/** Pointer to a const extent tree index node. */
typedef const EXTEXTENTIDX *PCEXTEXTENTIDX;


/**
 * Extent tree leaf node.
 */
typedef struct EXTEXTENT
{
    /** 0x00: First file block number this extent covers. */
    uint32_t    iBlock;
    /** 0x04: Number of blocks covered by this extent. */
    uint16_t    cBlocks;
    /** 0x06: Block number this extent points to (upper 32bits). */
    uint16_t    offStartHigh;
    /** 0x08: Block number this extent points to (lower 32bits). */
    uint32_t    offStartLow;
} EXTEXTENT;
AssertCompileSize(EXTEXTENT, 12);
/** Pointer to a leaf node. */
typedef EXTEXTENT *PEXTEXTENT;
/** Pointer to a const leaf node. */
typedef const EXTEXTENT *PCEXTEXTENT;

/** Length field limit for a populated extent, fields greater than that limit indicate a sparse extent. */
#define EXT_EXTENT_LENGTH_LIMIT                      UINT16_C(32768)


/**
 * Directory entry.
 */
typedef struct EXTDIRENTRY
{
    /** 0x00: Inode number being referenced by this entry. */
    uint32_t    iInodeRef;
    /** 0x04: Record length of this directory entry in bytes (multiple of 4). */
    uint16_t    cbRecord;
    /** 0x06: Version dependent data. */
    union
    {
        /** Original. */
        struct
        {
            /** Name length in bytes (maximum 255). */
            uint16_t    cbName;
        } v1;
        /** Version 2. */
        struct
        {
            /** Name length in bytes (maximum 255). */
            uint8_t     cbName;
            /** File type (EXT_DIRENTRY_TYPE_XXX). */
            uint8_t     uType;
        } v2;
    } u;
    /** 0x08: File name - variable in size. */
    char        achName[1];
} EXTDIRENTRY;
/** Pointer to a directory entry. */
typedef EXTDIRENTRY *PEXTDIRENTRY;
/** Poiner to a const directory entry. */
typedef const EXTDIRENTRY *PCEXTDIRENTRY;


/**
 * Extended directory entry with the maximum size (263 bytes).
 */
#pragma pack(1)
typedef union EXTDIRENTRYEX
{
    /** The directory entry. */
    EXTDIRENTRY Core;
    /** The byte view. */
    uint8_t     au8[263];
} EXTDIRENTRYEX;
#pragma pack()
AssertCompileSize(EXTDIRENTRYEX, 263);
/** Pointer to an extended directory entry. */
typedef EXTDIRENTRYEX *PEXTDIRENTRYEX;
/** Pointer to a const extended directory entry. */
typedef const EXTDIRENTRYEX *PCEXTDIRENTRYEX;


/** @name EXT_DIRENTRY_TYPE_XXX - file type
 * @{ */
/** Entry is of unknown file type. */
#define EXT_DIRENTRY_TYPE_UNKNOWN                    0
/** Entry is regular file. */
#define EXT_DIRENTRY_TYPE_REGULAR                    1
/** Entry is another directory. */
#define EXT_DIRENTRY_TYPE_DIRECTORY                  2
/** Entry is a character device. */
#define EXT_DIRENTRY_TYPE_CHAR                       3
/** Entry is a block device. */
#define EXT_DIRENTRY_TYPE_BLOCK                      4
/** Entry is a FIFO. */
#define EXT_DIRENTRY_TYPE_FIFO                       5
/** Entry is a socket. */
#define EXT_DIRENTRY_TYPE_SOCKET                     6
/** Entry is a symlink. */
#define EXT_DIRENTRY_TYPE_SYMLINK                    7
/** Entry is a checksum and uses EXTDIRENTRYCHKSUM. */
#define EXT_DIRENTRY_TYPE_CHKSUM                     0xde
/** @} */


/**
 * Tail directory entry (for checksumming).
 */
typedef struct EXTDIRENTRYCHKSUM
{
    /** 0x00: Reserved, must be 0 (overlays with EXTDIRENTRY::iNodeRef). */
    uint32_t    u32Rsvd;
    /** 0x04: Record length (must be 12). */
    uint16_t    cbRecord;
    /** 0x06: Reserved (overlays with EXTDIRENTRY::u::v1::cbName). */
    uint8_t     u8Rsvd;
    /** 0x07: File type (must be 0xde). */
    uint8_t     uType;
    /** 0x08: Checksum. */
    uint32_t    u32Chksum;
} EXTDIRENTRYCHKSUM;
/** Pointer to a tail directory entry. */
typedef EXTDIRENTRYCHKSUM *PEXTDIRENTRYCHKSUM;
/** Pointer to const tail directory entry. */
typedef const EXTDIRENTRYCHKSUM *PCEXTDIRENTRYCHKSUM;


/** @} */

#endif /* !IPRT_INCLUDED_formats_ext_h */

