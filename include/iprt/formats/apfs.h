/* $Id: apfs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, APFS (Apple File System) format.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_apfs_h
#define IPRT_INCLUDED_formats_apfs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_apfs    Apple File System structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */

/*
 * The filesystem structures were retrieved from:
 * https://developer.apple.com/support/downloads/Apple-File-System-Reference.pdf
 */

/** Physical address of an on-disk block. */
typedef int64_t                 APFSPADDR;
/** Object identifier. */
typedef uint64_t                APFSOID;
/** Transaction identifier. */
typedef uint64_t                APFSXID;

/** Invalid object ID. */
#define APFS_OID_INVALID        UINT64_C(0)
/** Number of reserved object IDs for special structures. */
#define APFS_OID_RSVD_CNT       1024
/** Object ID of a super block. */
#define APFS_OID_NX_SUPERBLOCK  UINT64_C(1)


/**
 * Range of physical addresses.
 */
typedef struct
{
    /** Start address of the range. */
    APFSPADDR                   PAddrStart;
    /** Size of the range in blocks.*/
    uint64_t                    cBlocks;
} APFSPRANGE;
/** Pointer to a APFS range. */
typedef APFSPRANGE *PAPFSPRANGE;
/** Pointer to a const APFS range. */
typedef const APFSPRANGE *PCAPFSPRANGE;

/** APFS UUID (compatible with our UUID definition). */
typedef RTUUID                  APFSUUID;

/** Maximum object checksum size. */
#define APFS_OBJ_MAX_CHKSUM_SZ  8

/**
 * APFS Object header.
 */
typedef struct APFSOBJPHYS
{
    /** The stored checksum of the object. */
    uint8_t                     abChkSum[APFS_OBJ_MAX_CHKSUM_SZ];
    /** Object ID. */
    APFSOID                     Oid;
    /** Transaction ID. */
    APFSXID                     Xid;
    /** Object type. */
    uint32_t                    u32Type;
    /** Object sub type. */
    uint32_t                    u32SubType;
} APFSOBJPHYS;
/** Pointer to an APFS object header. */
typedef APFSOBJPHYS *PAPFSOBJPHYS;
/** Pointer to a const APFS object header. */
typedef const APFSOBJPHYS *PCAPFSOBJPHYS;

#define APFS_OBJECT_TYPE_MASK         UINT32_C(0x0000ffff)
#define APFS_OBJECT_TYPE_FLAGS_MASK   UINT32_C(0xffff0000)

/**
 * APFS EFI jumpstart information.
 */
typedef struct APFSEFIJMPSTART
{
    /** Object header. */
    APFSOBJPHYS                 ObjHdr;
    /** The magic value. */
    uint32_t                    u32Magic;
    /** The version of the structure. */
    uint32_t                    u32Version;
    /** EFI file length in bytes. */
    uint32_t                    cbEfiFile;
    /** Number of extents describing the on disk blocks the file is stored in. */
    uint32_t                    cExtents;
    /** Reserved. */
    uint64_t                    au64Rsvd0[16];
    /** After this comes a variable size of APFSPRANGE extent structures. */
} APFSEFIJMPSTART;
/** Pointer to an APFS EFI jumpstart structure. */
typedef APFSEFIJMPSTART *PAPFSEFIJMPSTART;
/** Pointer to a const APFS EFI jumpstart structure. */
typedef const APFSEFIJMPSTART *PCAPFSEFIJMPSTART;

/** EFI jumpstart magic ('RDSJ'). */
#define APFS_EFIJMPSTART_MAGIC             RT_MAKE_U32_FROM_U8('J', 'S', 'D', 'R')
/** EFI jumpstart version. */
#define APFS_EFIJMPSTART_VERSION           UINT32_C(1)

/** Maximum number of filesystems supported in a single container. */
#define APFS_NX_SUPERBLOCK_FS_MAX          UINT32_C(100)
/** Maximum number of counters in the superblock. */
#define APFS_NX_SUPERBLOCK_COUNTERS_MAX    UINT32_C(32)
/** Number of entries in the ephemeral information array. */
#define APFS_NX_SUPERBLOCK_EPH_INFO_COUNT  UINT32_C(4)

/**
 * APFS super block.
 */
typedef struct
{
    /** Object header. */
    APFSOBJPHYS                 ObjHdr;
    /** The magic value. */
    uint32_t                    u32Magic;
    /** Block size in bytes. */
    uint32_t                    cbBlock;
    /** Number of blocks in the volume. */
    uint64_t                    cBlocks;
    /** Feature flags of the volume. */
    uint64_t                    fFeatures;
    /** Readonly compatible features. */
    uint64_t                    fRdOnlyCompatFeatures;
    /** Incompatible features. */
    uint64_t                    fIncompatFeatures;
    /** UUID of the volume. */
    APFSUUID                    Uuid;
    /** Next free object identifier to use for new objects. */
    APFSOID                     OidNext;
    /** Next free transaction identifier to use for new transactions. */
    APFSOID                     XidNext;
    /** Number of blocks used by the checkpoint descriptor area. */
    uint32_t                    cXpDescBlocks;
    /** Number of blocks used by the checkpoint data area. */
    uint32_t                    cXpDataBlocks;
    /** Base address of checkpoint descriptor area. */
    APFSPADDR                   PAddrXpDescBase;
    /** Base address of checkpoint data area. */
    APFSPADDR                   PAddrXpDataBase;
    /** Next index to use in the checkpoint descriptor area. */
    uint32_t                    idxXpDescNext;
    /** Next index to use in the checkpoint data area. */
    uint32_t                    idxXpDataNext;
    /** Number of blocks in the checkpoint descriptor area used by the checkpoint that this superblock belongs to. */
    uint32_t                    cXpDescLen;
    /** Index of the first valid item in the checkpoint data area. */
    uint32_t                    idxXpDataFirst;
    /** Number of blocks in the checkpoint data area used by the checkpoint that this superblock belongs to. */
    uint32_t                    cXpDataLen;
    /** Ephemeral object identifer of the space manager. */
    APFSOID                     OidSpaceMgr;
    /** Physical object identifier for the containers object map. */
    APFSOID                     OidOMap;
    /** Ephemeral object identifer for the reaper. */
    APFSOID                     OidReaper;
    /** Reserved for testing should be always zero on disk. */
    uint32_t                    u32TestType;
    /** Maximum number of filesystems which can be stored in this container. */
    uint32_t                    cFsMax;
    /** Array of filesystem object identifiers. */
    APFSOID                     aFsOids[APFS_NX_SUPERBLOCK_FS_MAX];
    /** Array of counters primarily used during debugging. */
    uint64_t                    aCounters[APFS_NX_SUPERBLOCK_COUNTERS_MAX];
    /** Range of blocks where no space will be allocated, used for shrinking a partition. */
    APFSPRANGE                  RangeBlocked;
    /** Physical object identifier of a tree keeping track of objects needing to be moved out of the block range. */
    APFSOID                     OidTreeEvictMapping;
    /** Container flags. */
    uint64_t                    fFlags;
    /** Address of the EFI jumpstart structure. */
    APFSPADDR                   PAddrEfiJmpStart;
    /** UUID of the containers Fusion set if available. */
    APFSUUID                    UuidFusion;
    /** Address of the containers keybag. */
    APFSPADDR                   PAddrKeyLocker;
    /** Array of fields used in the management of ephemeral data. */
    uint64_t                    au64EphemeralInfo[APFS_NX_SUPERBLOCK_EPH_INFO_COUNT];
    /** Reserved for testing. */
    APFSOID                     OidTest;
    /** Physical object identifier of the Fusion middle tree. */
    APFSOID                     OidFusionMt;
    /** Ephemeral object identifier of the Fusion write-back cache state. */
    APFSOID                     OidFusionWbc;
    /** Blocks used for the Fusion write-back cache area. */
    APFSPRANGE                  RangeFusionWbc;
} APFSNXSUPERBLOCK;
/** Pointer to a APFS super block structure. */
typedef APFSNXSUPERBLOCK *PAPFSNXSUPERBLOCK;
/** Pointer to a const APFS super block structure. */
typedef const APFSNXSUPERBLOCK *PCAPFSNXSUPERBLOCK;

/** Superblock magic value ('BSXN'). */
#define APFS_NX_SUPERBLOCK_MAGIC           RT_MAKE_U32_FROM_U8('N', 'X', 'S', 'B')

/** @} */

#endif /* !IPRT_INCLUDED_formats_apfs_h */

