/* $Id: fat.h 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT, File Allocation Table (FAT).
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_fat_h
#define IPRT_INCLUDED_formats_fat_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_fat    File Allocation Table (FAT) structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */


/** @name FAT Media byte values
 * @remarks This isn't as simple as it's made out to be here!
 * @{ */
#define FATBPB_MEDIA_FLOPPY_8           UINT8_C(0xe5)
#define FATBPB_MEDIA_FLOPPY_5_DOT_25    UINT8_C(0xed)
#define FATBPB_MEDIA_FLOPPY_3_DOT_5     UINT8_C(0xf0)
/* incomplete, figure out as needed... */

/** Checks if @a a_bMedia is a valid media byte. */
#define FATBPB_MEDIA_IS_VALID(a_bMedia) (   (uint8_t)(a_bMedia) >= 0xf8 \
                                         || (uint8_t)(a_bMedia) == 0xf0 \
                                         || (uint8_t)(a_bMedia) == 0xf4 /* obscure - msdos 2.11 */ \
                                         || (uint8_t)(a_bMedia) == 0xf5 /* obscure - msdos 2.11 */ \
                                         || (uint8_t)(a_bMedia) == 0xed /* obscure - tandy 2000 */ \
                                         || (uint8_t)(a_bMedia) == 0xe5 /* obscure - tandy 2000 */ )
/** @} */

/** Checks if @a a_bFatId is a valid FAT ID byte.
 * @todo uncertain whether 0xf4 and 0xf5 should be allowed here too. */
#define FAT_ID_IS_VALID(a_bFatId) (   (uint8_t)(a_bFatId) >= 0xf8 \
                                   || (uint8_t)(a_bFatId) == 0xf0 \
                                   || (uint8_t)(a_bFatId) == 0xf4 /* obscure - msdos 2.11 */ \
                                   || (uint8_t)(a_bFatId) == 0xf5 /* obscure - msdos 2.11 */ \
                                   || (uint8_t)(a_bFatId) == 0xed /* obscure, tandy 2000 */ \
                                   || (uint8_t)(a_bFatId) == 0xe5 /* obscure, tandy 2000 */ )

/**
 * The DOS 2.0 BIOS parameter block (BPB).
 *
 * This was the first DOS version with a BPB.
 */
#pragma pack(1)
typedef struct FATBPB20
{
    /** 0x0b / 0x00: The sector size in bytes. */
    uint16_t        cbSector;
    /** 0x0d / 0x02: Number of sectors per cluster. */
    uint8_t         cSectorsPerCluster;
    /** 0x0e / 0x03: Number of reserved sectors before the first FAT. */
    uint16_t        cReservedSectors;
    /** 0x10 / 0x05: Number of FATs. */
    uint8_t         cFats;
    /** 0x11 / 0x06: Max size of the root directory (0 for FAT32). */
    uint16_t        cMaxRootDirEntries;
    /** 0x13 / 0x08: Total sector count, zero if 32-bit count is used. */
    uint16_t        cTotalSectors16;
    /** 0x15 / 0x0a: Media ID. */
    uint8_t         bMedia;
    /** 0x16 / 0x0b: Number of sectors per FAT (0 for FAT32). */
    uint16_t        cSectorsPerFat;
} FATBPB20;
#pragma pack()
AssertCompileSize(FATBPB20, 0xd);
/** Pointer to a DOS 2.0 BPB. */
typedef FATBPB20 *PFATBPB20;
/** Pointer to a const DOS 2.0 BPB. */
typedef FATBPB20 const *PCFATBPB20;


/**
 * The DOS 3.0 BPB changes that survived.
 */
#pragma pack(1)
typedef struct FATBPB30CMN
{
    /** DOS v2.0 BPB.   */
    FATBPB20        Bpb20;
    /** 0x18 / 0x0d: Sectors per track. Zero means reserved and not used. */
    uint16_t        cSectorsPerTrack;
    /** 0x1a / 0x0f: Number of heads. Zero means reserved and not used. */
    uint16_t        cTracksPerCylinder;
} FATBPB30CMN;
#pragma pack()
AssertCompileSize(FATBPB30CMN, 0x11);

/**
 * The DOS 3.0 BPB.
 */
#pragma pack(1)
typedef struct FATBPB30
{
    /** DOS v3.0 BPB bits that survived.   */
    FATBPB30CMN     Core30;
    /** 0x1c / 0x11: Number of hidden sectors preceding the volume. This is zero
     * on unpartitioned media. */
    uint16_t        cHiddenSectors;
} FATBPB30;
#pragma pack()
AssertCompileSize(FATBPB30, 0x13);
/** Pointer to a DOS 3.0 BPB. */
typedef FATBPB30 *PFATBPB30;
/** Pointer to a const DOS 3.0 BPB. */
typedef FATBPB30 const *PCFATBPB30;

/**
 * The DOS 3.0 BPB, flattened structure.
 */
#pragma pack(1)
typedef struct FATBPB30FLAT
{
    /** @name New in DOS 2.0
     * @{ */
    /** 0x0b / 0x00: The sector size in bytes. */
    uint16_t        cbSector;
    /** 0x0d / 0x02: Number of sectors per cluster. */
    uint8_t         cSectorsPerCluster;
    /** 0x0e / 0x03: Number of reserved sectors before the first FAT. */
    uint16_t        cReservedSectors;
    /** 0x10 / 0x05: Number of FATs. */
    uint8_t         cFats;
    /** 0x11 / 0x06: Max size of the root directory (0 for FAT32). */
    uint16_t        cMaxRootDirEntries;
    /** 0x13 / 0x08: Total sector count, zero if 32-bit count is used. */
    uint16_t        cTotalSectors16;
    /** 0x15 / 0x0a: Media ID. */
    uint8_t         bMedia;
    /** 0x16 / 0x0b: Number of sectors per FAT (0 for FAT32). */
    uint16_t        cSectorsPerFat;
    /** @} */
    /** @name New in DOS 3.0
     * @{  */
    /** 0x18 / 0x0d: Sectors per track. Zero means reserved and not used. */
    uint16_t        cSectorsPerTrack;
    /** 0x1a / 0x0f: Number of heads. Zero means reserved and not used. */
    uint16_t        cTracksPerCylinder;
    /** 0x1c / 0x11: Number of hidden sectors preceding the volume. This is zero
     * on unpartitioned media. */
    uint16_t        cHiddenSectors;
    /** @} */
} FATBPB30FLAT;
#pragma pack()
AssertCompileSize(FATBPB30FLAT, 0x13);
/** Pointer to a flattened DOS 3.0 BPB. */
typedef FATBPB30FLAT *PFATBPB30FLAT;
/** Pointer to a const flattened DOS 3.0 BPB. */
typedef FATBPB30FLAT const *PCFATBPB30FLAT;


/**
 * The DOS 3.2 BPB.
 */
#pragma pack(1)
typedef struct FATBPB32
{
    /** DOS v3.0 BPB.   */
    FATBPB30        Bpb30;
    /** 0x1e / 0x13: Number of sectors, including the hidden ones.  This is ZERO
     * in DOS 3.31+. */
    uint16_t        cAnotherTotalSectors;
} FATBPB32;
#pragma pack()
AssertCompileSize(FATBPB32, 0x15);
/** Pointer to a DOS 3.2 BPB. */
typedef FATBPB32 *PFATBPB32;
/** Pointer to const a DOS 3.2 BPB. */
typedef FATBPB32 const *PCFATBPB32;

/**
 * The DOS 3.2 BPB, flattened structure.
 */
#pragma pack(1)
typedef struct FATBPB32FLAT
{
    /** @name New in DOS 2.0
     * @{ */
    /** 0x0b / 0x00: The sector size in bytes. */
    uint16_t        cbSector;
    /** 0x0d / 0x02: Number of sectors per cluster. */
    uint8_t         cSectorsPerCluster;
    /** 0x0e / 0x03: Number of reserved sectors before the first FAT. */
    uint16_t        cReservedSectors;
    /** 0x10 / 0x05: Number of FATs. */
    uint8_t         cFats;
    /** 0x11 / 0x06: Max size of the root directory (0 for FAT32). */
    uint16_t        cMaxRootDirEntries;
    /** 0x13 / 0x08: Total sector count, zero if 32-bit count is used. */
    uint16_t        cTotalSectors16;
    /** 0x15 / 0x0a: Media ID. */
    uint8_t         bMedia;
    /** 0x16 / 0x0b: Number of sectors per FAT (0 for FAT32). */
    uint16_t        cSectorsPerFat;
    /** @} */
    /** @name New in DOS 3.0
     * @{  */
    /** 0x18 / 0x0d: Sectors per track. Zero means reserved and not used. */
    uint16_t        cSectorsPerTrack;
    /** 0x1a / 0x0f: Number of heads. Zero means reserved and not used. */
    uint16_t        cTracksPerCylinder;
    /** 0x1c / 0x11: Number of hidden sectors preceding the volume. This is zero
     * on unpartitioned media. */
    uint16_t        cHiddenSectors;
    /** @} */
    /** @name New in DOS 3.2
     * @{  */
    /** 0x1e / 0x13: Number of sectors, including the hidden ones.  This is ZERO
     * in DOS 3.31+. */
    uint16_t        cAnotherTotalSectors;
    /** @} */
} FATBPB32FLAT;
#pragma pack()
AssertCompileSize(FATBPB32FLAT, 0x15);
/** Pointer to a flattened DOS 3.2 BPB. */
typedef FATBPB32FLAT *PFATBPB32FLAT;
/** Pointer to a const flattened DOS 3.2 BPB. */
typedef FATBPB32FLAT const *PCFATBPB32FLAT;


/**
 * The DOS 3.31 BPB.
 */
#pragma pack(1)
typedef struct FATBPB331
{
    /** DOS v3.0 BPB bits that survived.   */
    FATBPB30CMN     Core30;
    /** 0x1c / 0x11: Number of hidden sectors preceding the volume.  This is zero
     * on unpartitioned media.  Values higher than 65535 are complicated due to
     * the field overlapping FATBPB32::cAnotherTotalSectors */
    uint32_t        cHiddenSectors;
    /** 0x20 / 0x15: Total logical sectors.  Used if count >= 64K, otherwise
     *  FATBPB20::cTotalSectors16 is used.  Zero if 64-bit value used with FAT32. */
    uint32_t        cTotalSectors32;
} FATBPB331;
#pragma pack()
AssertCompileSize(FATBPB331, 0x19);
/** Pointer to a DOS 3.31 BPB. */
typedef FATBPB331 *PFATBPB331;
/** Pointer to a const DOS 3.31 BPB. */
typedef FATBPB331 const *PCFATBPB331;

/**
 * The DOS 3.31 BPB, flattened structure.
 */
#pragma pack(1)
typedef struct FATBPB331FLAT
{
    /** @name New in DOS 2.0
     * @{ */
    /** 0x0b / 0x00: The sector size in bytes. */
    uint16_t        cbSector;
    /** 0x0d / 0x02: Number of sectors per cluster. */
    uint8_t         cSectorsPerCluster;
    /** 0x0e / 0x03: Number of reserved sectors before the first FAT (0 for
     *  NTFS). */
    uint16_t        cReservedSectors;
    /** 0x10 / 0x05: Number of FATs (0 for NTFS). */
    uint8_t         cFats;
    /** 0x11 / 0x06: Max size of the root directory (0 for FAT32 & NTFS). */
    uint16_t        cMaxRootDirEntries;
    /** 0x13 / 0x08: Total sector count, zero if 32-bit count is used (and for
     * NTFS). */
    uint16_t        cTotalSectors16;
    /** 0x15 / 0x0a: Media ID. */
    uint8_t         bMedia;
    /** 0x16 / 0x0b: Number of sectors per FAT (0 for FAT32 & NTFS). */
    uint16_t        cSectorsPerFat;
    /** @} */
    /** @name New in DOS 3.0
     * @{ */
    /** 0x18 / 0x0d: Sectors per track. Zero means reserved and not used. */
    uint16_t        cSectorsPerTrack;
    /** 0x1a / 0x0f: Number of heads. Zero means reserved and not used. */
    uint16_t        cTracksPerCylinder;
    /** @} */
    /** @name New in DOS 3.31
     * @{ */
    /** 0x1c / 0x11: Number of hidden sectors preceding the volume.  This is zero
     * on unpartitioned media.  Values higher than 65535 are complicated due to
     * the field overlapping FATBPB32::cAnotherTotalSectors */
    uint32_t        cHiddenSectors;
    /** 0x20 / 0x15: Total logical sectors.  Used if count >= 64K, otherwise
     * FATBPB20::cTotalSectors16 is used.  Zero if 64-bit value used with FAT32.
     * (Zero for NTFS). */
    uint32_t        cTotalSectors32;
    /** @} */
} FATBPB331FLAT;
#pragma pack()
AssertCompileSize(FATBPB331FLAT, 0x19);
/** Pointer to a flattened DOS 3.31 BPB. */
typedef FATBPB331FLAT *PFATBPB331FLAT;
/** Pointer to a const flattened DOS 3.31 BPB. */
typedef FATBPB331FLAT const *PCFATBPB331FLAT;


/**
 * Extended BIOS parameter block (EBPB).
 */
#pragma pack(1)
typedef struct FATEBPB
{
    /** The BPB.  */
    FATBPB331FLAT   Bpb;

    /** 0x24 / 0x19: BIOS INT13 pysical drive number. */
    uint8_t         bInt13Drive;
    /** 0x25 / 0x1a: Reserved. NT used bit 0 for indicating dirty FS, and bit 1
     * for surface scan. */
    uint8_t         bReserved;
    /** 0x26 / 0x1b: Extended boot signature, FATEBPB_SIGNATURE or
     * FATEBPB_SIGNATURE_OLD. */
    uint8_t         bExtSignature;
    /** 0x27 / 0x1c: The volume serial number. */
    uint32_t        uSerialNumber;
    /** 0x2b / 0x20: The volume label (space padded).
     * @remarks Not available with FATEBPB_SIGNATURE_OLD  */
    char            achLabel[11];
    /** 0x36 / 0x2b: The file system type (space padded).
     * @remarks Not available with FATEBPB_SIGNATURE_OLD  */
    char            achType[8];
} FATEBPB;
#pragma pack()
AssertCompileSize(FATEBPB, 0x33);
/** Pointer to an extended BIOS parameter block. */
typedef FATEBPB *PFATEBPB;
/** Pointer to a const extended BIOS parameter block. */
typedef FATEBPB const *PCFATEBPB;

/** FATEBPB::bExtSignature value. */
#define FATEBPB_SIGNATURE       UINT8_C(0x29)
/** FATEBPB::bExtSignature value used by OS/2 1.0-1.1 and PC DOS 3.4.  These
 * does not have the volume and file system type. */
#define FATEBPB_SIGNATURE_OLD   UINT8_C(0x28)

/**FATEBPB::achType value for FAT12. */
#define FATEBPB_TYPE_FAT12      "FAT12   "
/**FATEBPB::achType value for FAT16. */
#define FATEBPB_TYPE_FAT16      "FAT16   "
/**FATEBPB::achType value for FAT12/FAT16. */
#define FATEBPB_TYPE_FAT        "FAT32   "


/**
 * FAT32 Extended BIOS parameter block (EBPB).
 */
#pragma pack(1)
typedef struct FAT32EBPB
{
    /** The BPB.  */
    FATBPB331FLAT   Bpb;

    /** 0x24 / 0x19: Number of sectors per FAT.
     * @note To avoid confusion with the FATEBPB signature, values which result in
     *       0x00280000 or 0x00290000 when masked by 0x00ff0000 must not be used. */
    uint32_t        cSectorsPerFat32;
    /** 0x28 / 0x1d: Flags pertaining to FAT mirroring and other stuff. */
    uint16_t        fFlags;
    /** 0x2a / 0x1f: FAT32 version number (FAT32EBPB_VERSION_0_0). */
    uint16_t        uVersion;
    /** 0x2c / 0x21: Cluster number of the root directory. */
    uint32_t        uRootDirCluster;
    /** 0x30 / 0x25: Logical sector number of the information sector. */
    uint16_t        uInfoSectorNo;
    /** 0x32 / 0x27: Logical sector number of boot sector copy. */
    uint16_t        uBootSectorCopySectorNo;
    /** 0x34 / 0x29: Reserved, zero (or 0xf6) filled, preserve. */
    uint8_t         abReserved[12];

    /** 0x40 / 0x35: BIOS INT13 pysical drive number
     * @remarks Same as FATEBPB::bInt13Drive. */
    uint8_t         bInt13Drive;
    /** 0x41 / 0x36: Reserved.
     * @remarks Same as FATEBPB::bReserved. */
    uint8_t         bReserved;
    /** 0x42 / 0x37: Extended boot signature (FATEBPB_SIGNATURE, or
     *  FATEBPB_SIGNATURE_OLD in some special cases).
     * @remarks Same as FATEBPB::bExtSignature. */
    uint8_t         bExtSignature;
    /** 0x43 / 0x38: The volume serial number.
     * @remarks Same as FATEBPB::uSerialNumber. */
    uint32_t        uSerialNumber;
    /** 0x47 / 0x3c: The volume label (space padded).
     * @remarks Not available with FATEBPB_SIGNATURE_OLD
     * @remarks Same as FATEBPB::achLabel. */
    char            achLabel[11];
    /** 0x52 / 0x47: The file system type (space padded), or 64-bit logical sector
     * count if both other count fields are zero.  In the latter case, the type is
     * moved to the OEM name field (FATBOOTSECTOR::achOemName).
     *
     * @remarks Not available with FATEBPB_SIGNATURE_OLD
     * @remarks Same as FATEBPB::achType. */
    union
    {
        /** Type string variant.  */
        char        achType[8];
        /** Total sector count if 4G or higher. */
        uint64_t    cTotalSectors64;
    } u;
} FAT32EBPB;
#pragma pack()
AssertCompileSize(FAT32EBPB, 0x4f);
/** Pointer to a FAT32 extended BIOS parameter block. */
typedef FAT32EBPB *PFAT32EBPB;
/** Pointer to a const FAT32 extended BIOS parameter block. */
typedef FAT32EBPB const *PCFAT32EBPB;

/** FAT32 version 0.0 (FAT32EBPB::uVersion). */
#define FAT32EBPB_VERSION_0_0   UINT16_C(0x0000)


/**
 * NTFS extended BIOS parameter block (NTFSEBPB).
 */
#pragma pack(1)
typedef struct NTFSEBPB
{
    /** The BPB.  */
    FATBPB331FLAT   Bpb;

    /** 0x24 / 0x19: BIOS INT13 pysical drive number.
     * @note Same location as FATEBPB::bInt13Drive. */
    uint8_t         bInt13Drive;
    /** 0x25 / 0x1a: Reserved / flags */
    uint8_t         bReserved;
    /** 0x26 / 0x1b: Extended boot signature (NTFSEBPB_SIGNATURE).
     * @note Same location as FATEBPB::bExtSignature. */
    uint8_t         bExtSignature;
    /** 0x27 / 0x1c: Reserved   */
    uint8_t         bReserved2;

    /** 0x28 / 0x1d: Number of sectors. */
    uint64_t        cSectors;
    /** 0x30 / 0x25: Logical cluster number of the master file table (MFT).   */
    uint64_t        uLcnMft;
    /** 0x38 / 0x2d: Logical cluster number of the MFT mirror. */
    uint64_t        uLcnMftMirror;
    /** 0x40 / 0x35: Logical clusters per file record segment.
     * This is a shift count if negative.  */
    int8_t          cClustersPerMftRecord;
    /** 0x41 / 0x36: Reserved. */
    uint8_t         abReserved3[3];
    /** 0x44 / 0x39: The default logical clusters count per index node.
     * This is a shift count if negative.  */
    int8_t          cClustersPerIndexNode;
    /** 0x45 / 0x3a: Reserved. */
    uint8_t         abReserved4[3];
    /** 0x48 / 0x3d: Volume serial number.
     * @note This is larger than the the FAT serial numbers. */
    uint64_t        uSerialNumber;
    /** 0x50 / 0x45: Checksum. */
    uint32_t        uChecksum;
} NTFSEBPB;
#pragma pack()
AssertCompileSize(NTFSEBPB, 0x49);
/** Pointer to a NTFS extended BIOS parameter block. */
typedef NTFSEBPB *PNTFSEBPB;
/** Pointer to a const NTFS extended BIOS parameter block. */
typedef NTFSEBPB const *PCNTFSEBPB;

/** NTFS EBPB signature (NTFSEBPB::bExtSignature). */
#define NTFSEBPB_SIGNATURE   UINT8_C(0x80)


/**
 * FAT boot sector layout.
 */
#pragma pack(1)
typedef struct FATBOOTSECTOR
{
    /** 0x000: DOS 2.0+ jump sequence. */
    uint8_t         abJmp[3];
    /** 0x003: OEM name (who formatted this volume). */
    char            achOemName[8];
    /** 0x00b: The BIOS parameter block.
     * This varies a lot in size. */
    union
    {
        FATBPB20        Bpb20;
        FATBPB30FLAT    Bpb30;
        FATBPB32FLAT    Bpb32;
        FATBPB331FLAT   Bpb331;
        FATEBPB         Ebpb;
        FAT32EBPB       Fat32Ebpb;
        NTFSEBPB        Ntfs;
    } Bpb;
    /** 0x05a: Bootloader code/data/stuff. */
    uint8_t             abStuff[0x1a3];
    /** 0x1fd: Old drive number location (DOS 3.2-3.31). */
    uint8_t             bOldInt13Drive;
    /** 0x1fe: DOS signature (FATBOOTSECTOR_SIGNATURE). */
    uint16_t            uSignature;
} FATBOOTSECTOR;
#pragma pack()
AssertCompileSize(FATBOOTSECTOR, 0x200);
/** Pointer to a FAT boot sector. */
typedef FATBOOTSECTOR *PFATBOOTSECTOR;
/** Pointer to a const FAT boot sector. */
typedef FATBOOTSECTOR const *PCFATBOOTSECTOR;

/** Boot sector signature (FATBOOTSECTOR::uSignature). */
#define FATBOOTSECTOR_SIGNATURE     UINT16_C(0xaa55)



/**
 * FAT32 info sector (follows the boot sector).
 */
typedef struct FAT32INFOSECTOR
{
    /** 0x000: Signature \#1 (FAT32INFOSECTOR_SIGNATURE_1). */
    uint32_t        uSignature1;
    /** Reserved, should be zero. */
    uint8_t         abReserved1[0x1E0];
    /** 0x1e4: Signature \#1 (FAT32INFOSECTOR_SIGNATURE_2). */
    uint32_t        uSignature2;
    /** 0x1e8: Last known number of free clusters (informational). */
    uint32_t        cFreeClusters;
    /** 0x1ec: Last allocated cluster number (informational).  This could be used as
     * an allocation hint when searching for a free cluster. */
    uint32_t        cLastAllocatedCluster;
    /** 0x1f0: Reserved, should be zero, preserve. */
    uint8_t         abReserved2[12];
    /** 0x1fc: Signature \#3 (FAT32INFOSECTOR_SIGNATURE_3). */
    uint32_t        uSignature3;
} FAT32INFOSECTOR;
AssertCompileSize(FAT32INFOSECTOR, 0x200);
/** Pointer to a FAT32 info sector. */
typedef FAT32INFOSECTOR *PFAT32INFOSECTOR;
/** Pointer to a const FAT32 info sector. */
typedef FAT32INFOSECTOR const *PCFAT32INFOSECTOR;

#define FAT32INFOSECTOR_SIGNATURE_1     UINT32_C(0x41615252)
#define FAT32INFOSECTOR_SIGNATURE_2     UINT32_C(0x61417272)
#define FAT32INFOSECTOR_SIGNATURE_3     UINT32_C(0xaa550000)


/** @name Special FAT cluster numbers and limits.
 * @{ */
#define FAT_FIRST_DATA_CLUSTER          2                       /**< The first data cluster. */

#define FAT_MAX_FAT12_TOTAL_CLUSTERS    UINT32_C(0x00000ff6)    /**< Maximum number of clusters in a 12-bit FAT . */
#define FAT_MAX_FAT16_TOTAL_CLUSTERS    UINT32_C(0x0000fff6)    /**< Maximum number of clusters in a 16-bit FAT . */
#define FAT_MAX_FAT32_TOTAL_CLUSTERS    UINT32_C(0x0ffffff6)    /**< Maximum number of clusters in a 32-bit FAT . */

#define FAT_LAST_FAT12_DATA_CLUSTER     UINT32_C(0x00000ff5)    /**< The last possible data cluster for FAT12. */
#define FAT_LAST_FAT16_DATA_CLUSTER     UINT32_C(0x0000fff5)    /**< The last possible data cluster for FAT16. */
#define FAT_LAST_FAT32_DATA_CLUSTER     UINT32_C(0x0ffffff5)    /**< The last possible data cluster for FAT32. */

#define FAT_MAX_FAT12_DATA_CLUSTERS     UINT32_C(0x00000ff4)    /**< Maximum number of data clusters for FAT12. */
#define FAT_MAX_FAT16_DATA_CLUSTERS     UINT32_C(0x0000fff4)    /**< Maximum number of data clusters for FAT16. */
#define FAT_MAX_FAT32_DATA_CLUSTERS     UINT32_C(0x0ffffff4)    /**< Maximum number of data clusters for FAT32. */

#define FAT_MIN_FAT12_DATA_CLUSTERS     UINT32_C(0x00000001)    /**< Maximum number of data clusters for FAT12. */
#define FAT_MIN_FAT16_DATA_CLUSTERS     UINT32_C(0x00000ff5)    /**< Maximum number of data clusters for FAT16. */
#define FAT_MIN_FAT32_DATA_CLUSTERS     UINT32_C(0x0000fff5)    /**< Maximum number of data clusters for FAT32. */

#define FAT_FIRST_FAT12_EOC             UINT32_C(0x00000ff8)    /**< The first end-of-file-cluster number for FAT12. */
#define FAT_FIRST_FAT16_EOC             UINT32_C(0x0000fff8)    /**< The first end-of-file-cluster number for FAT16. */
#define FAT_FIRST_FAT32_EOC             UINT32_C(0x0ffffff8)    /**< The first end-of-file-cluster number for FAT32. */
/** @} */


/**
 * FAT directory entry.
 */
typedef struct FATDIRENTRY
{
    /** 0x00: The directory entry name.
     * First character serves as a flag to indicate deleted or not. */
    uint8_t         achName[8+3];
    /** 0x0b: Attributes (FAT_ATTR_XXX). */
    uint8_t         fAttrib;
    /** 0x0c: NT case flags (FATDIRENTRY_CASE_F_XXX). */
    uint8_t         fCase;
    /** 0x0d: Birth milliseconds (DOS 7.0+ w/VFAT). */
    uint8_t         uBirthCentiseconds;
    /** 0x0e: Birth time (DOS 7.0+ w/VFAT). */
    uint16_t        uBirthTime;
    /** 0x10: Birth date (DOS 7.0+ w/VFAT). */
    uint16_t        uBirthDate;
    /** 0x12: Access date (DOS 7.0+ w/ACCDATA in Config.sys). */
    uint16_t        uAccessDate;
    union
    {
        /** 0x14: High cluster word for FAT32.   */
        uint16_t    idxClusterHigh;
        /** 0x14: Index of extended attributes (FAT16/FAT12). */
        uint16_t    idxEAs;
    } u;
    /** 0x16: Modify time (PC-DOS 1.1+, MS-DOS 1.20+).  */
    uint16_t        uModifyTime;
    /** 0x18: Modify date. */
    uint16_t        uModifyDate;
    /** 0x1a: The data cluster index. */
    uint16_t        idxCluster;
    /** 0x1c: The file size. */
    uint32_t        cbFile;
} FATDIRENTRY;
AssertCompileSize(FATDIRENTRY, 0x20);
AssertCompileMemberOffset(FATDIRENTRY, fAttrib, 0x0b);
AssertCompileMemberOffset(FATDIRENTRY, fCase, 0x0c);
AssertCompileMemberOffset(FATDIRENTRY, uBirthCentiseconds, 0x0d);
AssertCompileMemberOffset(FATDIRENTRY, uBirthTime, 0x0e);
AssertCompileMemberOffset(FATDIRENTRY, uBirthDate, 0x10);
AssertCompileMemberOffset(FATDIRENTRY, uAccessDate, 0x12);
AssertCompileMemberOffset(FATDIRENTRY, u, 0x14);
AssertCompileMemberOffset(FATDIRENTRY, uModifyTime, 0x16);
AssertCompileMemberOffset(FATDIRENTRY, uModifyDate, 0x18);
AssertCompileMemberOffset(FATDIRENTRY, idxCluster, 0x1a);
AssertCompileMemberOffset(FATDIRENTRY, cbFile, 0x1c);
/** Pointer to a FAT directory entry. */
typedef FATDIRENTRY *PFATDIRENTRY;
/** Pointer to a FAT directory entry. */
typedef FATDIRENTRY const *PCFATDIRENTRY;


/** @name FAT_ATTR_XXX - FATDIRENTRY::fAttrib flags.
 * @{ */
#define FAT_ATTR_READONLY           UINT8_C(0x01)
#define FAT_ATTR_HIDDEN             UINT8_C(0x02)
#define FAT_ATTR_SYSTEM             UINT8_C(0x04)
#define FAT_ATTR_VOLUME             UINT8_C(0x08)
#define FAT_ATTR_DIRECTORY          UINT8_C(0x10)
#define FAT_ATTR_ARCHIVE            UINT8_C(0x20)
#define FAT_ATTR_DEVICE             UINT8_C(0x40)
#define FAT_ATTR_RESERVED           UINT8_C(0x80)
#define FAT_ATTR_NAME_SLOT          UINT8_C(0x0f) /**< Special attribute value for FATDIRNAMESLOT. */
/** @} */

/** @name FATDIRENTRY_CASE_F_XXX - FATDIRENTRY::fCase flags.
 * @{ */
/** Lower cased base name (first 8 chars). */
#define FATDIRENTRY_CASE_F_LOWER_BASE   UINT8_C(0x08)
/** Lower cased filename extension (last 3 chars). */
#define FATDIRENTRY_CASE_F_LOWER_EXT    UINT8_C(0x10)
/** @} */

/** @name FATDIRENTRY_CH0_XXX - FATDIRENTRY::achName[0]
 * @{ */
/** Deleted entry. */
#define FATDIRENTRY_CH0_DELETED         UINT8_C(0xe5)
/** End of used directory entries (MS-DOS 1.25+, PC-DOS 2.0+). */
#define FATDIRENTRY_CH0_END_OF_DIR      UINT8_C(0x00)
/** The special dot or dot-dot dir aliases (MS-DOS 1.40+, PC-DOS 2.0+).
 * @remarks 0x2e is the ascii table entry of the '.' character.  */
#define FATDIRENTRY_CH0_DOT_ALIAS       UINT8_C(0x2e)
/** Escaped 0xe5 leadcharacter (DOS 3.0+). */
#define FATDIRENTRY_CH0_ESC_E5          UINT8_C(0x05)
/** @} */


/**
 * FAT directory alias name slot.
 *
 * Each slot holds 13 UTF-16 (/ UCS-2) characters, so it takes 20 slots to cover
 * a 255 character long name.
 */
#pragma pack(1)
typedef struct FATDIRNAMESLOT
{
    /** The slot sequence number. */
    uint8_t         idSlot;
    /** The first 5 name chars.
     * @remarks misaligned  */
    RTUTF16         awcName0[5];
    /** Attributes (FAT_ATTR_XXX). */
    uint8_t         fAttrib;
    /** Always zero. */
    uint8_t         fZero;
    /** Alias checksum. */
    uint8_t         bChecksum;
    /** The next 6 name chars. */
    RTUTF16         awcName1[6];
    /** Always zero (usually cluster entry). */
    uint16_t        idxZero;
    /** The next 2 name chars. */
    RTUTF16         awcName2[2];
} FATDIRNAMESLOT;
#pragma pack()
AssertCompileSize(FATDIRNAMESLOT, 0x20);
/** Pointer to a FAT directory entry. */
typedef FATDIRNAMESLOT *PFATDIRNAMESLOT;
/** Pointer to a FAT directory entry. */
typedef FATDIRNAMESLOT const *PCFATDIRNAMESLOT;

/** Slot ID flag indicating that it's the first slot. */
#define FATDIRNAMESLOT_FIRST_SLOT_FLAG  UINT8_C(0x40)
/** Highest slot ID recognized.  This allows for 260 characters, however many
 * implementation limits it to 255 or 250. */
#define FATDIRNAMESLOT_HIGHEST_SLOT_ID  UINT8_C(0x14)
/** Max number of slots recognized.  (This is the same as the higest slot ID
 * because the 0 isn't a valid ID.) */
#define FATDIRNAMESLOT_MAX_SLOTS        FATDIRNAMESLOT_HIGHEST_SLOT_ID
/** Number of UTF-16 units per slot. */
#define FATDIRNAMESLOT_CHARS_PER_SLOT   (5 + 6 + 2)



/**
 * FAT directory entry union.
 */
typedef union FATDIRENTRYUNION
{
    /** Regular entry view. */
    FATDIRENTRY     Entry;
    /** Name slot view. */
    FATDIRNAMESLOT  Slot;
} FATDIRENTRYUNION;
AssertCompileSize(FATDIRENTRYUNION, 0x20);
/** Pointer to a FAT directory entry union. */
typedef FATDIRENTRYUNION *PFATDIRENTRYUNION;
/** Pointer to a const FAT directory entry union. */
typedef FATDIRENTRYUNION const *PCFATDIRENTRYUNION;

/** @} */

#endif /* !IPRT_INCLUDED_formats_fat_h */

