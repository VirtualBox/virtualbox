/* $Id: VDICore.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual Disk Image (VDI), Core Code Header (internal).
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

#ifndef VBOX_INCLUDED_SRC_Storage_VDICore_h
#define VBOX_INCLUDED_SRC_Storage_VDICore_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/vd.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/asm.h>


/*******************************************************************************
*   Constants And Macros, Structures and Typedefs                              *
*******************************************************************************/

/** Image info, not handled anyhow.
 *  Must be less than 64 bytes in length, including the trailing 0.
 */
#define VDI_IMAGE_FILE_INFO   "<<< Oracle VirtualBox Disk Image >>>\n"

/** The Sector size.
 * Currently we support only 512 bytes sectors.
 */
#define VDI_GEOMETRY_SECTOR_SIZE    (512)
/**  512 = 2^^9 */
#define VDI_GEOMETRY_SECTOR_SHIFT   (9)

/**
 * Harddisk geometry.
 */
#pragma pack(1)
typedef struct VDIDISKGEOMETRY
{
    /** Cylinders. */
    uint32_t    cCylinders;
    /** Heads. */
    uint32_t    cHeads;
    /** Sectors per track. */
    uint32_t    cSectors;
    /** Sector size. (bytes per sector) */
    uint32_t    cbSector;
} VDIDISKGEOMETRY, *PVDIDISKGEOMETRY;
#pragma pack()

/** Image signature. */
#define VDI_IMAGE_SIGNATURE   (0xbeda107f)

/**
 * Pre-Header to be stored in image file - used for version control.
 */
#pragma pack(1)
typedef struct VDIPREHEADER
{
    /** Just text info about image type, for eyes only. */
    char            szFileInfo[64];
    /** The image signature (VDI_IMAGE_SIGNATURE). */
    uint32_t        u32Signature;
    /** The image version (VDI_IMAGE_VERSION). */
    uint32_t        u32Version;
} VDIPREHEADER, *PVDIPREHEADER;
#pragma pack()

/**
 * Size of szComment field of HDD image header.
 */
#define VDI_IMAGE_COMMENT_SIZE    256

/**
 * Header to be stored in image file, VDI_IMAGE_VERSION_MAJOR = 0.
 * Prepended by VDIPREHEADER.
 */
#pragma pack(1)
typedef struct VDIHEADER0
{
    /** The image type (VDI_IMAGE_TYPE_*). */
    uint32_t        u32Type;
    /** Image flags (VDI_IMAGE_FLAGS_*). */
    uint32_t        fFlags;
    /** Image comment. (UTF-8) */
    char            szComment[VDI_IMAGE_COMMENT_SIZE];
    /** Legacy image geometry (previous code stored PCHS there). */
    VDIDISKGEOMETRY LegacyGeometry;
    /** Size of disk (in bytes). */
    uint64_t        cbDisk;
    /** Block size. (For instance VDI_IMAGE_BLOCK_SIZE.) */
    uint32_t        cbBlock;
    /** Number of blocks. */
    uint32_t        cBlocks;
    /** Number of allocated blocks. */
    uint32_t        cBlocksAllocated;
    /** UUID of image. */
    RTUUID          uuidCreate;
    /** UUID of image's last modification. */
    RTUUID          uuidModify;
    /** Only for secondary images - UUID of primary image. */
    RTUUID          uuidLinkage;
} VDIHEADER0, *PVDIHEADER0;
#pragma pack()

/**
 * Header to be stored in image file, VDI_IMAGE_VERSION_MAJOR = 1,
 * VDI_IMAGE_VERSION_MINOR = 1. Prepended by VDIPREHEADER.
 */
#pragma pack(1)
typedef struct VDIHEADER1
{
    /** Size of this structure in bytes. */
    uint32_t        cbHeader;
    /** The image type (VDI_IMAGE_TYPE_*). */
    uint32_t        u32Type;
    /** Image flags (VDI_IMAGE_FLAGS_*). */
    uint32_t        fFlags;
    /** Image comment. (UTF-8) */
    char            szComment[VDI_IMAGE_COMMENT_SIZE];
    /** Offset of Blocks array from the beginning of image file.
     * Should be sector-aligned for HDD access optimization. */
    uint32_t        offBlocks;
    /** Offset of image data from the beginning of image file.
     * Should be sector-aligned for HDD access optimization. */
    uint32_t        offData;
    /** Legacy image geometry (previous code stored PCHS there). */
    VDIDISKGEOMETRY LegacyGeometry;
    /** Was BIOS HDD translation mode, now unused. */
    uint32_t        u32Dummy;
    /** Size of disk (in bytes). */
    uint64_t        cbDisk;
    /** Block size. (For instance VDI_IMAGE_BLOCK_SIZE.) Should be a power of 2! */
    uint32_t        cbBlock;
    /** Size of additional service information of every data block.
     * Prepended before block data. May be 0.
     * Should be a power of 2 and sector-aligned for optimization reasons. */
    uint32_t        cbBlockExtra;
    /** Number of blocks. */
    uint32_t        cBlocks;
    /** Number of allocated blocks. */
    uint32_t        cBlocksAllocated;
    /** UUID of image. */
    RTUUID          uuidCreate;
    /** UUID of image's last modification. */
    RTUUID          uuidModify;
    /** Only for secondary images - UUID of previous image. */
    RTUUID          uuidLinkage;
    /** Only for secondary images - UUID of previous image's last modification. */
    RTUUID          uuidParentModify;
} VDIHEADER1, *PVDIHEADER1;
#pragma pack()

/**
 * Header to be stored in image file, VDI_IMAGE_VERSION_MAJOR = 1,
 * VDI_IMAGE_VERSION_MINOR = 1, the slightly changed variant necessary as the
 * old released code doesn't support changing the minor version at all.
 */
#pragma pack(1)
typedef struct VDIHEADER1PLUS
{
    /** Size of this structure in bytes. */
    uint32_t        cbHeader;
    /** The image type (VDI_IMAGE_TYPE_*). */
    uint32_t        u32Type;
    /** Image flags (VDI_IMAGE_FLAGS_*). */
    uint32_t        fFlags;
    /** Image comment. (UTF-8) */
    char            szComment[VDI_IMAGE_COMMENT_SIZE];
    /** Offset of blocks array from the beginning of image file.
     * Should be sector-aligned for HDD access optimization. */
    uint32_t        offBlocks;
    /** Offset of image data from the beginning of image file.
     * Should be sector-aligned for HDD access optimization. */
    uint32_t        offData;
    /** Legacy image geometry (previous code stored PCHS there). */
    VDIDISKGEOMETRY LegacyGeometry;
    /** Was BIOS HDD translation mode, now unused. */
    uint32_t        u32Dummy;
    /** Size of disk (in bytes). */
    uint64_t        cbDisk;
    /** Block size. (For instance VDI_IMAGE_BLOCK_SIZE.) Should be a power of 2! */
    uint32_t        cbBlock;
    /** Size of additional service information of every data block.
     * Prepended before block data. May be 0.
     * Should be a power of 2 and sector-aligned for optimization reasons. */
    uint32_t        cbBlockExtra;
    /** Number of blocks. */
    uint32_t        cBlocks;
    /** Number of allocated blocks. */
    uint32_t        cBlocksAllocated;
    /** UUID of image. */
    RTUUID          uuidCreate;
    /** UUID of image's last modification. */
    RTUUID          uuidModify;
    /** Only for secondary images - UUID of previous image. */
    RTUUID          uuidLinkage;
    /** Only for secondary images - UUID of previous image's last modification. */
    RTUUID          uuidParentModify;
    /** LCHS image geometry (new field in VDI1.2 version. */
    VDIDISKGEOMETRY LCHSGeometry;
} VDIHEADER1PLUS, *PVDIHEADER1PLUS;
#pragma pack()

/**
 * Header structure for all versions.
 */
typedef struct VDIHEADER
{
    unsigned        uVersion;
    union
    {
        VDIHEADER0    v0;
        VDIHEADER1    v1;
        VDIHEADER1PLUS v1plus;
    } u;
} VDIHEADER, *PVDIHEADER;

/**
 * File alignment boundary for both the block array and data area. Should be
 * at least the size of a physical sector on disk for performance reasons.
 * Bumped to 1MB because SSDs tend to have 8kb per page so we don't have to worry
 * about proper alignment in the near future again. */
#define VDI_DATA_ALIGN _1M

/** Block 'pointer'. */
typedef uint32_t    VDIIMAGEBLOCKPOINTER;
/** Pointer to a block 'pointer'. */
typedef VDIIMAGEBLOCKPOINTER *PVDIIMAGEBLOCKPOINTER;

/**
 * Block marked as free is not allocated in image file, read from this
 * block may returns any random data.
 */
#define VDI_IMAGE_BLOCK_FREE   ((VDIIMAGEBLOCKPOINTER)~0)

/**
 * Block marked as zero is not allocated in image file, read from this
 * block returns zeroes.
 */
#define VDI_IMAGE_BLOCK_ZERO   ((VDIIMAGEBLOCKPOINTER)~1)

/**
 * Block 'pointer' >= VDI_IMAGE_BLOCK_UNALLOCATED indicates block is not
 * allocated in image file.
 */
#define VDI_IMAGE_BLOCK_UNALLOCATED   (VDI_IMAGE_BLOCK_ZERO)
#define IS_VDI_IMAGE_BLOCK_ALLOCATED(bp)   (bp < VDI_IMAGE_BLOCK_UNALLOCATED)

#define GET_MAJOR_HEADER_VERSION(ph) (VDI_GET_VERSION_MAJOR((ph)->uVersion))
#define GET_MINOR_HEADER_VERSION(ph) (VDI_GET_VERSION_MINOR((ph)->uVersion))

/** @name VDI image types
 * @{ */
typedef enum VDIIMAGETYPE
{
    /** Normal dynamically growing base image file. */
    VDI_IMAGE_TYPE_NORMAL = 1,
    /** Preallocated base image file of a fixed size. */
    VDI_IMAGE_TYPE_FIXED,
    /** Dynamically growing image file for undo/commit changes support. */
    VDI_IMAGE_TYPE_UNDO,
    /** Dynamically growing image file for differencing support. */
    VDI_IMAGE_TYPE_DIFF,

    /** First valid image type value. */
    VDI_IMAGE_TYPE_FIRST  = VDI_IMAGE_TYPE_NORMAL,
    /** Last valid image type value. */
    VDI_IMAGE_TYPE_LAST   = VDI_IMAGE_TYPE_DIFF
} VDIIMAGETYPE;
/** Pointer to VDI image type. */
typedef VDIIMAGETYPE *PVDIIMAGETYPE;
/** @} */

/*******************************************************************************
*   Internal Functions for header access                                       *
*******************************************************************************/
DECLINLINE(VDIIMAGETYPE) getImageType(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return (VDIIMAGETYPE)ph->u.v0.u32Type;
        case 1: return (VDIIMAGETYPE)ph->u.v1.u32Type;
    }
    AssertFailed();
    return (VDIIMAGETYPE)0;
}

DECLINLINE(unsigned) getImageFlags(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0:
            /* VDI image flag conversion to VD image flags. */
            return ph->u.v0.fFlags << 8;
        case 1:
            /* VDI image flag conversion to VD image flags. */
            return ph->u.v1.fFlags << 8;
    }
    AssertFailed();
    return 0;
}

DECLINLINE(char *) getImageComment(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return &ph->u.v0.szComment[0];
        case 1: return &ph->u.v1.szComment[0];
    }
    AssertFailed();
    return NULL;
}

DECLINLINE(unsigned) getImageBlocksOffset(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return (sizeof(VDIPREHEADER) + sizeof(VDIHEADER0));
        case 1: return ph->u.v1.offBlocks;
    }
    AssertFailed();
    return 0;
}

DECLINLINE(uint32_t) getImageDataOffset(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return sizeof(VDIPREHEADER) + sizeof(VDIHEADER0) + \
                       (ph->u.v0.cBlocks * sizeof(VDIIMAGEBLOCKPOINTER));
        case 1: return ph->u.v1.offData;
    }
    AssertFailed();
    return 0;
}

DECLINLINE(void) setImageDataOffset(PVDIHEADER ph, uint32_t offData)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return;
        case 1: ph->u.v1.offData = offData; return;
    }
    AssertFailed();
}

DECLINLINE(PVDIDISKGEOMETRY) getImageLCHSGeometry(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return NULL;
        case 1:
            switch (GET_MINOR_HEADER_VERSION(ph))
            {
                case 1:
                    if (ph->u.v1.cbHeader < sizeof(ph->u.v1plus))
                        return NULL;
                    else
                        return &ph->u.v1plus.LCHSGeometry;
            }
    }
    AssertFailed();
    return NULL;
}

DECLINLINE(uint64_t) getImageDiskSize(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return ph->u.v0.cbDisk;
        case 1: return ph->u.v1.cbDisk;
    }
    AssertFailed();
    return 0;
}

DECLINLINE(void) setImageDiskSize(PVDIHEADER ph, uint64_t cbDisk)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: ph->u.v0.cbDisk = cbDisk; return;
        case 1: ph->u.v1.cbDisk = cbDisk; return;
    }
    AssertFailed();
}

DECLINLINE(unsigned) getImageBlockSize(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return ph->u.v0.cbBlock;
        case 1: return ph->u.v1.cbBlock;
    }
    AssertFailed();
    return 0;
}

DECLINLINE(unsigned) getImageExtraBlockSize(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return 0;
        case 1: return ph->u.v1.cbBlockExtra;
    }
    AssertFailed();
    return 0;
}

DECLINLINE(unsigned) getImageBlocks(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return ph->u.v0.cBlocks;
        case 1: return ph->u.v1.cBlocks;
    }
    AssertFailed();
    return 0;
}

DECLINLINE(void) setImageBlocks(PVDIHEADER ph, unsigned cBlocks)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: ph->u.v0.cBlocks = cBlocks; return;
        case 1: ph->u.v1.cBlocks = cBlocks; return;
    }
    AssertFailed();
}


DECLINLINE(unsigned) getImageBlocksAllocated(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return ph->u.v0.cBlocksAllocated;
        case 1: return ph->u.v1.cBlocksAllocated;
    }
    AssertFailed();
    return 0;
}

DECLINLINE(void) setImageBlocksAllocated(PVDIHEADER ph, unsigned cBlocks)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: ph->u.v0.cBlocksAllocated = cBlocks; return;
        case 1: ph->u.v1.cBlocksAllocated = cBlocks; return;
    }
    AssertFailed();
}

#ifdef _MSC_VER
# pragma warning(disable:4366) /* (harmless "misalignment") */
#endif

DECLINLINE(PRTUUID) getImageCreationUUID(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return &ph->u.v0.uuidCreate;
        case 1: return &ph->u.v1.uuidCreate;
    }
    AssertFailed();
    return NULL;
}

DECLINLINE(PRTUUID) getImageModificationUUID(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return &ph->u.v0.uuidModify;
        case 1: return &ph->u.v1.uuidModify;
    }
    AssertFailed();
    return NULL;
}

DECLINLINE(PRTUUID) getImageParentUUID(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 0: return &ph->u.v0.uuidLinkage;
        case 1: return &ph->u.v1.uuidLinkage;
    }
    AssertFailed();
    return NULL;
}

DECLINLINE(PRTUUID) getImageParentModificationUUID(PVDIHEADER ph)
{
    switch (GET_MAJOR_HEADER_VERSION(ph))
    {
        case 1: return &ph->u.v1.uuidParentModify;
    }
    AssertFailed();
    return NULL;
}

#ifdef _MSC_VER
# pragma warning(default:4366)
#endif

/**
 * Image structure
 */
typedef struct VDIIMAGEDESC
{
    /** Opaque storage handle. */
    PVDIOSTORAGE            pStorage;
    /** Image open flags, VD_OPEN_FLAGS_*. */
    unsigned                uOpenFlags;
    /** Image pre-header. */
    VDIPREHEADER            PreHeader;
    /** Image header. */
    VDIHEADER               Header;
    /** Pointer to a block array. */
    PVDIIMAGEBLOCKPOINTER   paBlocks;
    /** Pointer to the block array for back resolving (used if discarding is enabled). */
    unsigned               *paBlocksRev;
    /** fFlags copy from image header, for speed optimization. */
    unsigned                uImageFlags;
    /** Start offset of block array in image file, here for speed optimization. */
    unsigned                offStartBlocks;
    /** Start offset of data in image file, here for speed optimization. */
    unsigned                offStartData;
    /** Block mask for getting the offset into a block from a byte hdd offset. */
    unsigned                uBlockMask;
    /** Block shift value for converting byte hdd offset into paBlock index. */
    unsigned                uShiftOffset2Index;
    /** Offset of data from the beginning of block. */
    unsigned                offStartBlockData;
    /** Total size of image block (including the extra data). */
    unsigned                cbTotalBlockData;
    /** Allocation Block Size */
    unsigned                cbAllocationBlock;
    /** Container filename. (UTF-8) */
    const char             *pszFilename;
    /** Physical geometry of this image (never actually stored). */
    VDGEOMETRY              PCHSGeometry;
    /** Pointer to the per-disk VD interface list. */
    PVDINTERFACE            pVDIfsDisk;
    /** Pointer to the per-image VD interface list. */
    PVDINTERFACE            pVDIfsImage;
    /** Error interface. */
    PVDINTERFACEERROR       pIfError;
    /** I/O interface. */
    PVDINTERFACEIOINT       pIfIo;
    /** Current size of the image (used for range validation when reading). */
    uint64_t                cbImage;
    /** The static region list. */
    VDREGIONLIST            RegionList;
} VDIIMAGEDESC, *PVDIIMAGEDESC;

/**
 * Async block discard states.
 */
typedef enum VDIBLOCKDISCARDSTATE
{
    /** Invalid. */
    VDIBLOCKDISCARDSTATE_INVALID = 0,
    /** Read the last block. */
    VDIBLOCKDISCARDSTATE_READ_BLOCK,
    /** Write block into the hole. */
    VDIBLOCKDISCARDSTATE_WRITE_BLOCK,
    /** Update metadata. */
    VDIBLOCKDISCARDSTATE_UPDATE_METADATA,
    /** 32bit hack. */
    VDIBLOCKDISCARDSTATE_32BIT_HACK = 0x7fffffff
} VDIBLOCKDISCARDSTATE;

/**
 * Async block discard structure.
 */
typedef struct VDIBLOCKDISCARDASYNC
{
    /** State of the block discard. */
    VDIBLOCKDISCARDSTATE    enmState;
    /** Pointer to the block data. */
    void                   *pvBlock;
    /** Block index in the block table. */
    unsigned                uBlock;
    /** Block pointer to the block to discard. */
    VDIIMAGEBLOCKPOINTER    ptrBlockDiscard;
    /** Index of the last block in the reverse block table. */
    unsigned                idxLastBlock;
    /** Index of the last block in the block table (gathered from the reverse block table). */
    unsigned                uBlockLast;
} VDIBLOCKDISCARDASYNC, *PVDIBLOCKDISCARDASYNC;

/**
 * Async image expansion state.
 */
typedef struct VDIASYNCBLOCKALLOC
{
    /** Number of blocks allocated. */
    unsigned                cBlocksAllocated;
    /** Block index to allocate. */
    unsigned                uBlock;
} VDIASYNCBLOCKALLOC, *PVDIASYNCBLOCKALLOC;

/**
 * Endianess conversion direction.
 */
typedef enum VDIECONV
{
    /** Host to file endianess. */
    VDIECONV_H2F = 0,
    /** File to host endianess. */
    VDIECONV_F2H
} VDIECONV;

#endif /* !VBOX_INCLUDED_SRC_Storage_VDICore_h */

