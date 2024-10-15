/* $Id: iso9660.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, ISO 9660 File System
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

#ifndef IPRT_INCLUDED_formats_iso9660_h
#define IPRT_INCLUDED_formats_iso9660_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_iso9660    ISO 9660 structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */


/** The (default) logical sectors size of ISO 9660. */
#define ISO9660_SECTOR_SIZE                 2048
/** The (default) sector offset mask of ISO 9660. */
#define ISO9660_SECTOR_OFFSET_MASK          2047
/** Maximum filename length (level 2 & 3). */
#define ISO9660_MAX_NAME_LEN                30


/** Accessor for ISO9660U16 and ISO9660U32 that retrievs the member value for
 *  the host endianess. */
#ifdef RT_BIG_ENDIAN
# define ISO9660_GET_ENDIAN(a_pInt) ((a_pInt)->be)
#else
# define ISO9660_GET_ENDIAN(a_pInt) ((a_pInt)->le)
#endif


/**
 * ISO 9660 16-bit unsigned integer type.
 */
typedef struct ISO9660U16
{
    /** Little endian. */
    uint16_t            le;
    /** Big endian. */
    uint16_t            be;
} ISO9660U16;
/** Pointer to an ISO 9660 16-bit unsigned integer type. */
typedef ISO9660U16 *PISO9660U16;
/** Pointer to a const ISO 9660 16-bit unsigned integer type. */
typedef ISO9660U16 const *PCISO9660U16;

/** ISO 9660 big endian 16-bit unsigned integer. */
typedef uint16_t        ISO9660U16BE;


/**
 * ISO 9660 32-bit unsigned integer type.
 */
typedef struct ISO9660U32
{
    /** Little endian. */
    uint32_t            le;
    /** Big endian. */
    uint32_t            be;
} ISO9660U32;
/** Pointer to an ISO 9660 32-bit unsigned integer type. */
typedef ISO9660U32 *PISO9660U32;
/** Pointer to a const ISO 9660 32-bit unsigned integer type. */
typedef ISO9660U32 const *PCISO9660U32;

/** ISO 9660 little endian 32-bit unsigned integer. */
typedef uint32_t        ISO9660U32LE;
/** ISO 9660 big endian 32-bit unsigned integer. */
typedef uint32_t        ISO9660U32BE;

/**
 * ISO 9660 timestamp (date & time).
 */
typedef struct ISO9660TIMESTAMP
{
    /** 0x00: For digit year (0001-9999). */
    char                achYear[4];
    /** 0x04: Month of the year (01-12). */
    char                achMonth[2];
    /** 0x06: Day of month (01-31). */
    char                achDay[2];
    /** 0x08: Hour of day (00-23). */
    char                achHour[2];
    /** 0x0a: Minute of hour (00-59). */
    char                achMinute[2];
    /** 0x0c: Second of minute (00-59). */
    char                achSecond[2];
    /** 0x0e: Hundreth of second (00-99). */
    char                achCentisecond[2];
    /** 0x10: The UTC (GMT) offset in 15 min units. */
    int8_t              offUtc;
} ISO9660TIMESTAMP;
AssertCompileSize(ISO9660TIMESTAMP, 17);
/** Pointer to an ISO 9660 timestamp. */
typedef ISO9660TIMESTAMP *PISO9660TIMESTAMP;
/** Pointer to a const ISO 9660 timestamp. */
typedef ISO9660TIMESTAMP const *PCISO9660TIMESTAMP;

/**
 * ISO 9660 record timestamp (date & time).
 */
typedef struct ISO9660RECTIMESTAMP
{
    /** 0: Years since 1900. */
    uint8_t             bYear;
    /** 1: Month of year (1-12). */
    uint8_t             bMonth;
    /** 2: Day of month (1-31). */
    uint8_t             bDay;
    /** 3: Hour of day (0-23). */
    uint8_t             bHour;
    /** 4: Minute of hour (0-59). */
    uint8_t             bMinute;
    /** 5: Second of minute (0-59). */
    uint8_t             bSecond;
    /** 6: The UTC (GMT) offset in 15 min units. */
    int8_t              offUtc;
} ISO9660RECTIMESTAMP;
AssertCompileSize(ISO9660RECTIMESTAMP, 7);
/** Pointer to an ISO 9660 record timestamp. */
typedef ISO9660RECTIMESTAMP *PISO9660RECTIMESTAMP;
/** Pointer to a const ISO 9660 record timestamp. */
typedef ISO9660RECTIMESTAMP const *PCISO9660RECTIMESTAMP;


/**
 * ISO 9660 directory record.
 */
#pragma pack(1)
typedef struct ISO9660DIRREC
{
    /** 0x00: Length of this record in bytes. */
    uint8_t             cbDirRec;
    /** 0x01: Extended attribute record length in logical blocks. */
    uint8_t             cExtAttrBlocks;
    /** 0x02: Location of extent (logical block number).
     * @note Misaligned. */
    ISO9660U32          offExtent;
    /** 0x0a: Size of the data (file section).  Does not include EAs.
     * @note Misaligned. */
    ISO9660U32          cbData;
    /** 0x12: Recording time and date. */
    ISO9660RECTIMESTAMP RecTime;
    /** 0x19: File flags (ISO9660_FILE_FLAGS_XXX). */
    uint8_t             fFileFlags;
    /** 0x1a: File unit size for interlaved mode. */
    uint8_t             bFileUnitSize;
    /** 0x1b: Interlave gap size. */
    uint8_t             bInterleaveGapSize;
    /** 0x1c: Volume sequence number where the extent resides. */
    ISO9660U16          VolumeSeqNo;
    /** 0x20: Length of file identifier field. */
    uint8_t             bFileIdLength;
    /** 0x21: File identifier (d-characters or d1-characters). */
    char                achFileId[1];
    /* There are more fields following:
     *   - one byte optional padding so the following field is at an even boundrary.
     *   - system use field until cbDirRec is reached.
     */
} ISO9660DIRREC;
#pragma pack()
AssertCompileMemberOffset(ISO9660DIRREC, offExtent,     0x02);
AssertCompileMemberOffset(ISO9660DIRREC, cbData,        0x0a);
AssertCompileMemberOffset(ISO9660DIRREC, RecTime,       0x12);
AssertCompileMemberOffset(ISO9660DIRREC, fFileFlags,    0x19);
AssertCompileMemberOffset(ISO9660DIRREC, bFileIdLength, 0x20);
AssertCompileMemberOffset(ISO9660DIRREC, achFileId,     0x21);
/** Pointer to an ISO 9660 directory record. */
typedef ISO9660DIRREC *PISO9660DIRREC;
/** Pointer to a const ISO 9660 directory record. */
typedef ISO9660DIRREC const *PCISO9660DIRREC;

/** @name ISO9660_FILE_FLAGS_XXX
 * @{ */
/** Existence - Hide the file from the user. */
#define ISO9660_FILE_FLAGS_HIDDEN           UINT8_C(0x01)
/** Directory - Indicates a directory as apposed to a regular file (0). */
#define ISO9660_FILE_FLAGS_DIRECTORY        UINT8_C(0x02)
/** Assocated File - Indicates that the file is an associated file. */
#define ISO9660_FILE_FLAGS_ASSOCIATED_FILE  UINT8_C(0x04)
/** Record - Indicates specified file content record format (see EAs). */
#define ISO9660_FILE_FLAGS_RECORD           UINT8_C(0x08)
/** Protection - Indicates owner/group or permission protection in EAs. */
#define ISO9660_FILE_FLAGS_PROTECTION       UINT8_C(0x10)
/** Reserved bit, MBZ. */
#define ISO9660_FILE_FLAGS_RESERVED_5       UINT8_C(0x20)
/** Reserved bit, MBZ. */
#define ISO9660_FILE_FLAGS_RESERVED_6       UINT8_C(0x40)
/** Multi-extend - Indicates that this isn't the final record for the file.
 * @remarks Use for working around 4 GiB file size limitation. */
#define ISO9660_FILE_FLAGS_MULTI_EXTENT     UINT8_C(0x80)
/** @} */


/**
 * ISO 9660 path table record.
 */
#pragma pack(1)
typedef struct ISO9660PATHREC
{
    /** 0x00: Length of the achDirId field in bytes. */
    uint8_t             cbDirId;
    /** 0x01: Extended attribute record length in bytes? */
    uint8_t             cbExtAttr;
    /** 0x02: Location of extent (logical block number).
     * @note Endianess depends on table.
     * @note Misaligned. */
    uint32_t            offExtent;
    /** 0x06: Parent directory number.
     * @note Endianess depends on table.  */
    uint16_t            idParentRec;
    /** 0x08: Directory identifier (d-characters or d1-characters). */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char                achDirId[RT_FLEXIBLE_ARRAY];
    /* There will be a zero padding byte following if the directory identifier length is odd. */
} ISO9660PATHREC;
#pragma pack()
AssertCompileMemberOffset(ISO9660PATHREC, cbExtAttr,   0x01);
AssertCompileMemberOffset(ISO9660PATHREC, offExtent,   0x02);
AssertCompileMemberOffset(ISO9660PATHREC, idParentRec, 0x06);
AssertCompileMemberOffset(ISO9660PATHREC, achDirId,   0x08);
/** Pointer to an ISO 9660 path table record. */
typedef ISO9660PATHREC *PISO9660PATHREC;
/** Pointer to a const ISO 9660 path table record. */
typedef ISO9660PATHREC const *PCISO9660PATHREC;


/**
 * ISO 9660 extended attribute record.
 */
typedef struct ISO9660EXATTRREC
{
    /** 0x000: The owner ID. */
    ISO9660U16          idOwner;
    /** 0x004: The group ID. */
    ISO9660U16          idGroup;
    /** 0x008: File permissions (ISO9660_PERM_XXX). */
    ISO9660U16BE        fPermissions;
    /** 0x00a: File creation timestamp. */
    ISO9660TIMESTAMP    BirthTimestamp;
    /** 0x01b: File modification timestamp. */
    ISO9660TIMESTAMP    ModifyTimestamp;
    /** 0x02c: File expiration timestamp. */
    ISO9660TIMESTAMP    ExpireTimestamp;
    /** 0x03d: File effective timestamp. */
    ISO9660TIMESTAMP    EffectiveTimestamp;
    /** 0x04e: Record format. */
    uint8_t             bRecordFormat;
    /** 0x04f: Record attributes. */
    uint8_t             fRecordAttrib;
    /** 0x050: Record length. */
    ISO9660U16          RecordLength;
    /** 0x054: System identifier (a-characters or a1-characters). */
    char                achSystemId[0x20];
    /** 0x074: System specific bytes. */
    uint8_t             abSystemUse[64];
    /** 0x0b4: Extended attribute record version (ISO9660EXATTRREC_VERSION).   */
    uint8_t             bExtRecVersion;
    /** 0x0b5: Length of escape sequences. */
    uint8_t             cbEscapeSequences;
    /** 0x0b6: Reserved for the future, MBZ. */
    uint8_t             abReserved183[64];
    /** 0x0f6: Length of the application use field. */
    ISO9660U16          cbAppUse;
    /** 0x0fa: Variable sized application use field. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t             abAppUse[RT_FLEXIBLE_ARRAY];
    /* This is followed by escape sequences with length given by cbEscapeSequnces. */
} ISO9660EXATTRREC;
AssertCompileMemberOffset(ISO9660EXATTRREC, EffectiveTimestamp, 0x03d);
AssertCompileMemberOffset(ISO9660EXATTRREC, cbAppUse, 0x0f6);

/** The ISO9660EXATTRREC::bExtRecVersion value.    */
#define ISO9660EXATTRREC_VERSION        UINT8_C(0x01)

/** @name ISO9660_PERM_XXX - ISO9660EXATTRREC::fPermissions
 * @{ */
/** @todo figure out this weird permission stuff...   */
/** @} */


/**
 * ISO 9660 volume descriptor header.
 */
typedef struct ISO9660VOLDESCHDR
{
    /** Descriptor type ISO9660VOLDESC_TYPE_XXX. */
    uint8_t             bDescType;
    /** Standard identifier 'CD001'   */
    uint8_t             achStdId[5];
    /** The descriptor version. */
    uint8_t             bDescVersion;
    /* (This is followed by the descriptor specific data). */
} ISO9660VOLDESCHDR;
AssertCompileSize(ISO9660VOLDESCHDR, 7);
/** Pointer to a volume descriptor header.  */
typedef ISO9660VOLDESCHDR *PISO9660VOLDESCHDR;
/** Pointer to a const volume descriptor header. */
typedef ISO9660VOLDESCHDR const *PCISO9660VOLDESCHDR;

/** @name ISO9660VOLDESC_TYPE_XXX - volume descriptor types
 * @{ */
/** See ISO9660BOOTRECORD. */
#define ISO9660VOLDESC_TYPE_BOOT_RECORD     UINT8_C(0x00)
/** See ISO9660PRIMARYVOLDESC. */
#define ISO9660VOLDESC_TYPE_PRIMARY         UINT8_C(0x01)
/** See ISO9660SUPVOLDESC. */
#define ISO9660VOLDESC_TYPE_SUPPLEMENTARY   UINT8_C(0x02)
/** See ISO9660VOLPARTDESC. */
#define ISO9660VOLDESC_TYPE_PARTITION       UINT8_C(0x03)
/** Terminates the volume descriptor set.  Has no data (zeros), version is 1. */
#define ISO9660VOLDESC_TYPE_TERMINATOR      UINT8_C(0xff)
/** @} */

/** The value of ISO9660VOLDESCHDR::achStdId   */
#define ISO9660VOLDESC_STD_ID               "CD001"
#define ISO9660VOLDESC_STD_ID_0             'C'
#define ISO9660VOLDESC_STD_ID_1             'D'
#define ISO9660VOLDESC_STD_ID_2             '0'
#define ISO9660VOLDESC_STD_ID_3             '0'
#define ISO9660VOLDESC_STD_ID_4             '1'



/**
 * ISO 9660 boot record (volume descriptor).
 */
typedef struct ISO9660BOOTRECORD
{
    /** The volume descriptor header.
     * Type is ISO9660VOLDESC_TYPE_BOOT_RECORD and version
     * ISO9660BOOTRECORD_VERSION. */
    ISO9660VOLDESCHDR   Hdr;
    /** Boot system identifier string (a-characters). */
    char                achBootSystemId[32];
    /** Boot identifier (a-characters). */
    char                achBootId[32];
    /** Boot system specific content. */
    uint8_t             abBootSystemSpecific[1977];
} ISO9660BOOTRECORD;
AssertCompileSize(ISO9660BOOTRECORD, ISO9660_SECTOR_SIZE);
/** Pointer to an ISO 9660 boot record. */
typedef ISO9660BOOTRECORD *PISO9660BOOTRECORD;
/** Pointer to a const ISO 9660 boot record. */
typedef ISO9660BOOTRECORD const *PCISO9660BOOTRECORD;

/** The value of ISO9660BOOTRECORD::Hdr.uDescVersion. */
#define ISO9660BOOTRECORD_VERSION           UINT8_C(1)


/**
 * ISO 9660 boot record (volume descriptor), El Torito variant.
 */
#pragma pack(1)
typedef struct ISO9660BOOTRECORDELTORITO
{
    /** 0x000: The volume descriptor header.
     * Type is ISO9660VOLDESC_TYPE_BOOT_RECORD and version
     * ISO9660BOOTRECORD_VERSION. */
    ISO9660VOLDESCHDR   Hdr;
    /** 0x007: Boot system identifier string,
     * zero padded ISO9660BOOTRECORDELTORITO_BOOT_SYSTEM_ID. */
    char                achBootSystemId[32];
    /** 0x027: Boot identifier - all zeros. */
    char                achBootId[32];
    /** 0x047: Boot catalog location (block offset), always (?) little endian.
     * @note Misaligned. */
    uint32_t            offBootCatalog;
    /** 0x04b: Unused - all zeros. */
    uint8_t             abBootSystemSpecific[1973];
} ISO9660BOOTRECORDELTORITO;
#pragma pack()
AssertCompileSize(ISO9660BOOTRECORDELTORITO, ISO9660_SECTOR_SIZE);
/** Pointer to an ISO 9660 El Torito boot record. */
typedef ISO9660BOOTRECORDELTORITO *PISO9660BOOTRECORDELTORITO;
/** Pointer to a const ISO 9660 El Torito boot record. */
typedef ISO9660BOOTRECORDELTORITO const *PCISO9660BOOTRECORDELTORITO;

/** The value of ISO9660BOOTRECORDELTORITO::achBootSystemId (zero padded). */
#define ISO9660BOOTRECORDELTORITO_BOOT_SYSTEM_ID    "EL TORITO SPECIFICATION"


/**
 * ISO 9660 primary volume descriptor.
 */
typedef struct ISO9660PRIMARYVOLDESC
{
    /** 0x000: The volume descriptor header.
     * Type is ISO9660VOLDESC_TYPE_PRIMARY and version
     * ISO9660PRIMARYVOLDESC_VERSION. */
    ISO9660VOLDESCHDR   Hdr;
    /** 0x007: Explicit alignment zero padding. */
    uint8_t             bPadding8;
    /** 0x008: System identifier (a-characters). */
    char                achSystemId[32];
    /** 0x028: Volume identifier (d-characters). */
    char                achVolumeId[32];
    /** 0x048: Unused field, zero filled. */
    ISO9660U32          Unused73;
    /** 0x050: Volume space size in logical blocks (cbLogicalBlock). */
    ISO9660U32          VolumeSpaceSize;
    /** 0x058: Unused field(s), zero filled. */
    uint8_t             abUnused89[32];
    /** 0x078: The number of volumes in the volume set. */
    ISO9660U16          cVolumesInSet;
    /** 0x07c: Volume sequence number. */
    ISO9660U16          VolumeSeqNo;
    /** 0x080: Logical block size in bytes. */
    ISO9660U16          cbLogicalBlock;
    /** 0x084: Path table size. */
    ISO9660U32          cbPathTable;
    /** 0x08c: Type L(ittle endian) path table location (block offset). */
    ISO9660U32LE        offTypeLPathTable;
    /** 0x090: Optional type L(ittle endian) path table location (block offset). */
    ISO9660U32LE        offOptionalTypeLPathTable;
    /** 0x094: Type M (big endian) path table location (block offset). */
    ISO9660U32BE        offTypeMPathTable;
    /** 0x098: Optional type M (big endian) path table location (block offset). */
    ISO9660U32BE        offOptionalTypeMPathTable;
    /** 0x09c: Directory entry for the root directory (union). */
    union
    {
        uint8_t         ab[34];
        ISO9660DIRREC   DirRec;
    }                   RootDir;
    /** 0x0be: Volume set identifier (d-characters). */
    char                achVolumeSetId[128];
    /** 0x13e: Publisher identifier (a-characters).  Alternatively, it may refere to
     * a file in the root dir if it starts with 0x5f and restricts itself to 8
     * d-characters. */
    char                achPublisherId[128];
    /** 0x1be: Data preparer identifier (a-characters).
     * Same file reference alternative as previous field. */
    char                achDataPreparerId[128];
    /** 0x23e: Application identifier (a-characters).
     * Same file reference alternative as previous field. */
    char                achApplicationId[128];
    /** 0x2be: Copyright (root) file identifier (d-characters).
     * All spaces if none. */
    char                achCopyrightFileId[37];
    /** 0x2e3: Abstract (root) file identifier (d-characters).
     * All spaces if none. */
    char                achAbstractFileId[37];
    /** 0x308: Bibliographic file identifier (d-characters).
     * All spaces if none. */
    char                achBibliographicFileId[37];
    /** 0x32d: Volume creation date and time. */
    ISO9660TIMESTAMP    BirthTime;
    /** 0x33e: Volume modification date and time. */
    ISO9660TIMESTAMP    ModifyTime;
    /** 0x34f: Volume (data) expiration date and time.
     * If not specified, don't regard data as obsolete. */
    ISO9660TIMESTAMP    ExpireTime;
    /** 0x360: Volume (data) effective date and time.
     * If not specified, info can be used immediately. */
    ISO9660TIMESTAMP    EffectiveTime;
    /** 0x371: File structure version (ISO9660_FILE_STRUCTURE_VERSION). */
    uint8_t             bFileStructureVersion;
    /** 0x372: Reserve for future, MBZ. */
    uint8_t             bReserved883;
    /** 0x373: Reserve for future.
     * mkisofs & genisoimage & libisofs seems to space pad this most of the time.
     * Microsoft image (2.56) zero pads it.  isomd5sum uses it to store checksum
     * info for the iso and space pads it. */
    uint8_t             abAppUse[512];
    /** 0x573: Reserved for future standardization, MBZ. */
    uint8_t             abReserved1396[653];
} ISO9660PRIMARYVOLDESC;
AssertCompileSize(ISO9660PRIMARYVOLDESC, ISO9660_SECTOR_SIZE);
/** Pointer to a ISO 9660 primary volume descriptor. */
typedef ISO9660PRIMARYVOLDESC *PISO9660PRIMARYVOLDESC;
/** Pointer to a const ISO 9660 primary volume descriptor. */
typedef ISO9660PRIMARYVOLDESC const *PCISO9660PRIMARYVOLDESC;

/** The value of ISO9660PRIMARYVOLDESC::Hdr.uDescVersion. */
#define ISO9660PRIMARYVOLDESC_VERSION           UINT8_C(1)
/** The value of ISO9660PRIMARYVOLDESC::bFileStructureVersion and
 *  ISO9660SUPVOLDESC::bFileStructureVersion. */
#define ISO9660_FILE_STRUCTURE_VERSION          UINT8_C(1)



/**
 * ISO 9660 supplementary volume descriptor.
 *
 * This is in the large parts identicial to the primary descriptor, except it
 * have a few more fields where the primary one has reserved spaces.
 */
typedef struct ISO9660SUPVOLDESC
{
    /** 0x000: The volume descriptor header.
     * Type is ISO9660VOLDESC_TYPE_SUPPLEMENTARY and version
     * ISO9660SUPVOLDESC_VERSION. */
    ISO9660VOLDESCHDR   Hdr;
    /** 0x007: Volume flags (ISO9660SUPVOLDESC_VOL_F_XXX).
     * @note This is reserved in the primary volume descriptor. */
    uint8_t             fVolumeFlags;
    /** 0x008: System identifier (a1-characters) of system that can act upon
     * sectors 0 thru 15.
     * @note Purpose differs from primary description. */
    char                achSystemId[32];
    /** 0x028: Volume identifier (d1-characters).
     * @note Character set differs from primary description. */
    char                achVolumeId[32];
    /** 0x048: Unused field, zero filled. */
    ISO9660U32          Unused73;
    /** 0x050: Volume space size in logical blocks (cbLogicalBlock). */
    ISO9660U32          VolumeSpaceSize;
    /** 0x058: Escape sequences.
     * Complicated stuff, see ISO 2022 and ECMA-35.
     * @note This is reserved in the primary volume descriptor. */
    uint8_t             abEscapeSequences[32];
    /** 0x078: The number of volumes in the volume set. */
    ISO9660U16          cVolumesInSet;
    /** 0x07c: Volume sequence number. */
    ISO9660U16          VolumeSeqNo;
    /** 0x080: Logical block size in bytes. */
    ISO9660U16          cbLogicalBlock;
    /** 0x084: Path table size. */
    ISO9660U32          cbPathTable;
    /** 0x08c: Type L(ittle endian) path table location (block offset). */
    ISO9660U32LE        offTypeLPathTable;
    /** 0x090: Optional type L(ittle endian) path table location (block offset). */
    ISO9660U32LE        offOptionalTypeLPathTable;
    /** 0x094: Type M (big endian) path table location (block offset). */
    ISO9660U32BE        offTypeMPathTable;
    /** 0x098: Optional type M (big endian) path table location (block offset). */
    ISO9660U32BE        offOptionalTypeMPathTable;
    /** 0x09c: Directory entry for the root directory (union). */
    union
    {
        uint8_t         ab[34];
        ISO9660DIRREC   DirRec;
    }                   RootDir;
    /** 0x0be: Volume set identifier (d1-characters).
     * @note Character set differs from primary description. */
    char                achVolumeSetId[128];
    /** 0x13e: Publisher identifier (a1-characters).  Alternatively, it may refere
     * to a file in the root dir if it starts with 0x5f and restricts itself to 8
     * d1-characters.
     * @note Character set differs from primary description. */
    char                achPublisherId[128];
    /** 0x1be: Data preparer identifier (a1-characters).
     * Same file reference alternative as previous field.
     * @note Character set differs from primary description. */
    char                achDataPreparerId[128];
    /** 0x23e: Application identifier (a1-characters).
     * Same file reference alternative as previous field.
     * @note Character set differs from primary description. */
    char                achApplicationId[128];
    /** 0x2be: Copyright (root) file identifier (d1-characters).
     * All spaces if none.
     * @note Character set differs from primary description. */
    char                achCopyrightFileId[37];
    /** 0x2e3: Abstract (root) file identifier (d1-characters).
     * All spaces if none.
     * @note Character set differs from primary description. */
    char                achAbstractFileId[37];
    /** 0x308: Bibliographic file identifier (d1-characters).
     * All spaces if none.
     * @note Character set differs from primary description. */
    char                achBibliographicFileId[37];
    /** 0x32d: Volume creation date and time. */
    ISO9660TIMESTAMP    BirthTime;
    /** 0x33e: Volume modification date and time. */
    ISO9660TIMESTAMP    ModifyTime;
    /** 0x34f: Volume (data) expiration date and time.
     * If not specified, don't regard data as obsolete. */
    ISO9660TIMESTAMP    ExpireTime;
    /** 0x360: Volume (data) effective date and time.
     * If not specified, info can be used immediately. */
    ISO9660TIMESTAMP    EffectiveTime;
    /** 0x371: File structure version (ISO9660_FILE_STRUCTURE_VERSION). */
    uint8_t             bFileStructureVersion;
    /** 0x372: Reserve for future, MBZ. */
    uint8_t             bReserved883;
    /** 0x373: Reserve for future, MBZ. */
    uint8_t             abAppUse[512];
    /** 0x573: Reserved for future standardization, MBZ. */
    uint8_t             abReserved1396[653];
} ISO9660SUPVOLDESC;
AssertCompileSize(ISO9660SUPVOLDESC, ISO9660_SECTOR_SIZE);
/** Pointer to a ISO 9660 supplementary volume descriptor. */
typedef ISO9660SUPVOLDESC *PISO9660SUPVOLDESC;
/** Pointer to a const ISO 9660 supplementary volume descriptor. */
typedef ISO9660SUPVOLDESC const *PCISO9660SUPVOLDESC;
/** The value of ISO9660SUPVOLDESC::Hdr.uDescVersion. */
#define ISO9660SUPVOLDESC_VERSION               UINT8_C(1)

/** @name ISO9660SUPVOLDESC_VOL_F_XXX - ISO9660SUPVOLDESC::fVolumeFlags
 * @{ */
#define ISO9660SUPVOLDESC_VOL_F_ESC_ONLY_REG    UINT8_C(0x00)
#define ISO9660SUPVOLDESC_VOL_F_ESC_NOT_REG     UINT8_C(0x01)
/** @} */



/**
 * ISO 9660 volume partition descriptor.
 */
typedef struct ISO9660VOLPARTDESC
{
    /** 0x000: The volume descriptor header.
     * Type is ISO9660VOLDESC_TYPE_PARTITION and version
     * ISO9660VOLPARTDESC_VERSION. */
    ISO9660VOLDESCHDR   Hdr;
    /** 0x007: Alignment padding. */
    uint8_t             bPadding8;
    /** 0x008: System identifier (a-characters). */
    char                achSystemId[32];
    /** 0x028: Volume partition identifier (d-characters). */
    char                achVolumePartitionId[32];
    /** 0x048: The location of the partition (logical block number). */
    ISO9660U32          offVolumePartition;
    /** 0x050: The partition size in logical blocks (cbLogicalBlock). */
    ISO9660U32          VolumePartitionSize;
    /** 0x058: System specific data. */
    uint8_t             achSystemUse[1960];
} ISO9660VOLPARTDESC;
AssertCompileSize(ISO9660VOLPARTDESC, ISO9660_SECTOR_SIZE);
/** Pointer to an ISO 9660 volume partition description. */
typedef ISO9660VOLPARTDESC *PISO9660VOLPARTDESC;
/** Pointer to a const ISO 9660 volume partition description. */
typedef ISO9660VOLPARTDESC const *PCISO9660VOLPARTDESC;
/** The value of ISO9660VOLPARTDESC::Hdr.uDescVersion. */
#define ISO9660VOLPARTDESC_VERSION               UINT8_C(1)



/** @name Joliet escape sequence identifiers.
 *
 * These bytes appears in the supplementary volume descriptor field
 * abEscapeSequences.  The ISO9660SUPVOLDESC_VOL_F_ESC_NOT_REG flags will not
 * be set.
 *
 * @{ */
#define ISO9660_JOLIET_ESC_SEQ_0            UINT8_C(0x25)   /**< First escape sequence byte.*/
#define ISO9660_JOLIET_ESC_SEQ_1            UINT8_C(0x2f)   /**< Second escape sequence byte.*/
#define ISO9660_JOLIET_ESC_SEQ_2_LEVEL_1    UINT8_C(0x40)   /**< Third escape sequence byte: level 1 */
#define ISO9660_JOLIET_ESC_SEQ_2_LEVEL_2    UINT8_C(0x43)   /**< Third escape sequence byte: level 2 */
#define ISO9660_JOLIET_ESC_SEQ_2_LEVEL_3    UINT8_C(0x45)   /**< Third escape sequence byte: level 3 */
/** @} */


/** The size of an El Torito boot catalog entry. */
#define ISO9660_ELTORITO_ENTRY_SIZE         UINT32_C(0x20)

/**
 * El Torito boot catalog: Validation entry.
 *
 * This is the first entry in the boot catalog.  It is followed by a
 * ISO9660ELTORITODEFAULTENTRY, which in turn is followed by a
 * ISO9660ELTORITOSECTIONHEADER.
 */
typedef struct ISO9660ELTORITOVALIDATIONENTRY
{
    /** 0x00: The header ID (ISO9660_ELTORITO_HEADER_ID_VALIDATION_ENTRY). */
    uint8_t             bHeaderId;
    /** 0x01: The platform ID (ISO9660_ELTORITO_PLATFORM_ID_XXX). */
    uint8_t             bPlatformId;
    /** 0x02: Reserved, MBZ. */
    uint16_t            u16Reserved;
    /** 0x04: String ID of the developer of the CD/DVD-ROM. */
    char                achId[24];
    /** 0x1c: The checksum. */
    uint16_t            u16Checksum;
    /** 0x1e: Key byte 1 (ISO9660_ELTORITO_KEY_BYTE_1). */
    uint8_t             bKey1;
    /** 0x1f: Key byte 2 (ISO9660_ELTORITO_KEY_BYTE_2). */
    uint8_t             bKey2;
} ISO9660ELTORITOVALIDATIONENTRY;
AssertCompileSize(ISO9660ELTORITOVALIDATIONENTRY, ISO9660_ELTORITO_ENTRY_SIZE);
/** Pointer to an El Torito validation entry. */
typedef ISO9660ELTORITOVALIDATIONENTRY *PISO9660ELTORITOVALIDATIONENTRY;
/** Pointer to a const El Torito validation entry. */
typedef ISO9660ELTORITOVALIDATIONENTRY const *PCISO9660ELTORITOVALIDATIONENTRY;

/** ISO9660ELTORITOVALIDATIONENTRY::bKey1 value. */
#define ISO9660_ELTORITO_KEY_BYTE_1         UINT8_C(0x55)
/** ISO9660ELTORITOVALIDATIONENTRY::bKey2 value. */
#define ISO9660_ELTORITO_KEY_BYTE_2         UINT8_C(0xaa)


/** @name ISO9660_ELTORITO_HEADER_ID_XXX - header IDs.
 * @{ */
/** Header ID for a ISO9660ELTORITOVALIDATIONENTRY. */
#define ISO9660_ELTORITO_HEADER_ID_VALIDATION_ENTRY UINT8_C(0x01)
/** Header ID for a ISO9660ELTORITOSECTIONHEADER. */
#define ISO9660_ELTORITO_HEADER_ID_SECTION_HEADER       UINT8_C(0x90)
/** Header ID for the final ISO9660ELTORITOSECTIONHEADER. */
#define ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER UINT8_C(0x91)
/** @} */


/** @name ISO9660_ELTORITO_PLATFORM_ID_XXX - El Torito Platform IDs
 * @{ */
#define ISO9660_ELTORITO_PLATFORM_ID_X86    UINT8_C(0x00)   /**< 80x86 */
#define ISO9660_ELTORITO_PLATFORM_ID_PPC    UINT8_C(0x01)   /**< PowerPC */
#define ISO9660_ELTORITO_PLATFORM_ID_MAC    UINT8_C(0x02)   /**< Mac */
#define ISO9660_ELTORITO_PLATFORM_ID_EFI    UINT8_C(0xef)   /**< UEFI */
/** @} */


/**
 * El Torito boot catalog: Section header entry.
 *
 * A non-final section header entry is followed by
 * ISO9660ELTORITOSECTIONHEADER::cEntries ISO9660ELTORITOSECTIONTENTRY instances.
 */
typedef struct ISO9660ELTORITOSECTIONHEADER
{
    /** 0x00: Header ID - ISO9660_ELTORITO_HEADER_ID_SECTION_HEADER or
     * ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER (if final). */
    uint8_t             bHeaderId;
    /** 0x01: The platform ID (ISO9660_ELTORITO_PLATFORM_ID_XXX). */
    uint8_t             bPlatformId;
    /** 0x02: Number of entries in this section (i.e. following this header). */
    uint16_t            cEntries;
    /** 0x04: String ID for the section. */
    char                achSectionId[28];
} ISO9660ELTORITOSECTIONHEADER;
AssertCompileSize(ISO9660ELTORITOSECTIONHEADER, ISO9660_ELTORITO_ENTRY_SIZE);
/** Pointer to an El Torito section header entry. */
typedef ISO9660ELTORITOSECTIONHEADER *PISO9660ELTORITOSECTIONHEADER;
/** Pointer to a const El Torito section header entry. */
typedef ISO9660ELTORITOSECTIONHEADER const *PCISO9660ELTORITOSECTIONHEADER;


/**
 * El Torito boot catalog: Default (initial) entry.
 *
 * Followed by ISO9660ELTORITOSECTIONHEADER.
 *
 * Differs from ISO9660ELTORITOSECTIONENTRY in that it doesn't have a
 * selection criteria and no media flags (only type).
 */
typedef struct ISO9660ELTORITODEFAULTENTRY
{
    /** 0x00: Boot indicator (ISO9660_ELTORITO_BOOT_INDICATOR_XXX). */
    uint8_t             bBootIndicator;
    /** 0x01: Boot media type.  The first four bits are defined by
     * ISO9660_ELTORITO_BOOT_MEDIA_TYPE_XXX, whereas the top four bits MBZ. */
    uint8_t             bBootMediaType;
    /** 0x02: Load segment - load address divided by 0x10. */
    uint16_t            uLoadSeg;
    /** 0x04: System type from image partition table. */
    uint8_t             bSystemType;
    /** 0x05: Unused, MBZ. */
    uint8_t             bUnused;
    /** 0x06: Number of emulated 512 byte sectors to load. */
    uint16_t            cEmulatedSectorsToLoad;
    /** 0x08: Image location in the ISO (block offset), always (?) little endian. */
    uint32_t            offBootImage;
    /** 0x0c: Reserved, MBZ */
    uint8_t             abReserved[20];
} ISO9660ELTORITODEFAULTENTRY;
AssertCompileSize(ISO9660ELTORITODEFAULTENTRY, ISO9660_ELTORITO_ENTRY_SIZE);
/** Pointer to an El Torito default (initial) entry. */
typedef ISO9660ELTORITODEFAULTENTRY *PISO9660ELTORITODEFAULTENTRY;
/** Pointer to a const El Torito default (initial) entry. */
typedef ISO9660ELTORITODEFAULTENTRY const *PCISO9660ELTORITODEFAULTENTRY;


/**
 * El Torito boot catalg: Section entry.
 */
typedef struct ISO9660ELTORITOSECTIONENTRY
{
    /** 0x00: Boot indicator (ISO9660_ELTORITO_BOOT_INDICATOR_XXX). */
    uint8_t             bBootIndicator;
    /** 0x01: Boot media type and flags.  The first four bits are defined by
     * ISO9660_ELTORITO_BOOT_MEDIA_TYPE_XXX and the top four bits by
     * ISO9660_ELTORITO_BOOT_MEDIA_F_XXX. */
    uint8_t             bBootMediaType;
    /** 0x02: Load segment - load address divided by 0x10. */
    uint16_t            uLoadSeg;
    /** 0x04: System type from image partition table. */
    uint8_t             bSystemType;
    /** 0x05: Unused, MBZ. */
    uint8_t             bUnused;
    /** 0x06: Number of emulated 512 byte sectors to load. */
    uint16_t            cEmulatedSectorsToLoad;
    /** 0x08: Image location in the ISO (block offset), always (?) little endian. */
    uint32_t            offBootImage;
    /** 0x0c: Selection criteria type (ISO9660_ELTORITO_SEL_CRIT_TYPE_XXX). */
    uint8_t             bSelectionCriteriaType;
    /** 0x0c: Selection criteria specific data. */
    uint8_t             abSelectionCriteria[19];
} ISO9660ELTORITOSECTIONENTRY;
AssertCompileSize(ISO9660ELTORITOSECTIONENTRY, ISO9660_ELTORITO_ENTRY_SIZE);
/** Pointer to an El Torito default (initial) entry. */
typedef ISO9660ELTORITOSECTIONENTRY *PISO9660ELTORITOSECTIONENTRY;
/** Pointer to a const El Torito default (initial) entry. */
typedef ISO9660ELTORITOSECTIONENTRY const *PCISO9660ELTORITOSECTIONENTRY;


/** @name  ISO9660_ELTORITO_BOOT_INDICATOR_XXX - Boot indicators.
 * @{ */
#define ISO9660_ELTORITO_BOOT_INDICATOR_BOOTABLE        UINT8_C(0x88)
#define ISO9660_ELTORITO_BOOT_INDICATOR_NOT_BOOTABLE    UINT8_C(0x00)
/** @} */

/** @name ISO9660_ELTORITO_BOOT_MEDIA_TYPE_XXX - Boot media types.
 * @{ */
#define ISO9660_ELTORITO_BOOT_MEDIA_TYPE_NO_EMULATION   UINT8_C(0x0)
#define ISO9660_ELTORITO_BOOT_MEDIA_TYPE_FLOPPY_1_2_MB  UINT8_C(0x1)
#define ISO9660_ELTORITO_BOOT_MEDIA_TYPE_FLOPPY_1_44_MB UINT8_C(0x2)
#define ISO9660_ELTORITO_BOOT_MEDIA_TYPE_FLOPPY_2_88_MB UINT8_C(0x3)
#define ISO9660_ELTORITO_BOOT_MEDIA_TYPE_HARD_DISK      UINT8_C(0x4)
#define ISO9660_ELTORITO_BOOT_MEDIA_TYPE_MASK           UINT8_C(0xf) /**< The media type mask. */
/** @} */

/** @name ISO9660_ELTORITO_BOOT_MEDIA_F_XXX - Boot media flags.
 * These only applies to the section entry, not to the default (initial) entry.
 * @{ */
#define ISO9660_ELTORITO_BOOT_MEDIA_F_RESERVED          UINT8_C(0x10) /**< Reserved bit, MBZ. */
#define ISO9660_ELTORITO_BOOT_MEDIA_F_CONTINUATION      UINT8_C(0x20) /**< Contiunation entry follows. */
#define ISO9660_ELTORITO_BOOT_MEDIA_F_ATAPI_DRIVER      UINT8_C(0x40) /**< Image contains an ATAPI driver. */
#define ISO9660_ELTORITO_BOOT_MEDIA_F_SCSI_DRIVERS      UINT8_C(0x80) /**< Image contains SCSI drivers. */
#define ISO9660_ELTORITO_BOOT_MEDIA_F_MASK              UINT8_C(0xf0) /**< The media/entry flag mask. */
/** @} */

/** @name ISO9660_ELTORITO_SEL_CRIT_TYPE_XXX - Selection criteria type.
 * @{ */
#define ISO9660_ELTORITO_SEL_CRIT_TYPE_NONE             UINT8_C(0x00) /**< No selection criteria */
#define ISO9660_ELTORITO_SEL_CRIT_TYPE_LANG_AND_VERSION UINT8_C(0x01) /**< Language and version (IBM). */
/** @} */


/**
 * El Torito boot catalog: Section entry extension.
 *
 * This is used for carrying additional selection criteria data.  It follows
 * a ISO9660ELTORITOSECTIONENTRY.
 */
typedef struct ISO9660ELTORITOSECTIONENTRYEXT
{
    /** 0x00: Extension indicator (ISO9660_ELTORITO_SECTION_ENTRY_EXT_ID). */
    uint8_t             bExtensionId;
    /** 0x01: Selection criteria extension flags (ISO9660_ELTORITO_SECTION_ENTRY_EXT_F_XXX). */
    uint8_t             fFlags;
    /** 0x02: Selection critiera data. */
    uint8_t             abSelectionCriteria[30];
} ISO9660ELTORITOSECTIONENTRYEXT;
AssertCompileSize(ISO9660ELTORITOSECTIONENTRYEXT, ISO9660_ELTORITO_ENTRY_SIZE);
/** Pointer to an El Torito default (initial) entry. */
typedef ISO9660ELTORITOSECTIONENTRYEXT *PISO9660ELTORITOSECTIONENTRYEXT;
/** Pointer to a const El Torito default (initial) entry. */
typedef ISO9660ELTORITOSECTIONENTRYEXT const *PCISO9660ELTORITOSECTIONENTRYEXT;

/** Value of ISO9660ELTORITOSECTIONENTRYEXT::bExtensionId. */
#define ISO9660_ELTORITO_SECTION_ENTRY_EXT_ID   UINT8_C(0x44)

/** @name ISO9660_ELTORITO_SECTION_ENTRY_EXT_F_XXX - ISO9660ELTORITOSECTIONENTRYEXT::fFlags
 * @{ */
#define ISO9660_ELTORITO_SECTION_ENTRY_EXT_F_MORE           UINT8_C(0x20) /**< Further extension entries follows.  */
#define ISO9660_ELTORITO_SECTION_ENTRY_EXT_F_UNUSED_MASK    UINT8_C(0xef) /**< Mask of all unused bits. */
/** @} */


/**
 * Boot information table used by isolinux and GRUB2 El Torito boot files.
 */
typedef struct ISO9660SYSLINUXINFOTABLE
{
    /** 0x00/0x08: Offset of the primary volume descriptor (block offset). */
    uint32_t            offPrimaryVolDesc;
    /** 0x04/0x0c: Offset of the boot file (block offset). */
    uint32_t            offBootFile;
    /** 0x08/0x10: Size of the boot file in bytes. */
    uint32_t            cbBootFile;
    /** 0x0c/0x14: Boot file checksum.
     * This is the sum of all the 32-bit words in the image, start at the end of
     * this structure (i.e. offset 64). */
    uint32_t            uChecksum;
    /** 0x10/0x18: Reserved for future fun. */
    uint32_t            auReserved[10];
} ISO9660SYSLINUXINFOTABLE;
AssertCompileSize(ISO9660SYSLINUXINFOTABLE, 56);
/** Pointer to a syslinux boot information table.   */
typedef ISO9660SYSLINUXINFOTABLE *PISO9660SYSLINUXINFOTABLE;
/** Pointer to a const syslinux boot information table.   */
typedef ISO9660SYSLINUXINFOTABLE const *PCISO9660SYSLINUXINFOTABLE;

/** The file offset of the isolinux boot info table. */
#define ISO9660SYSLINUXINFOTABLE_OFFSET     8



/**
 * System Use Sharing Protocol Protocol (SUSP) header.
 */
typedef struct ISO9660SUSPHDR
{
    /** Signature byte 1. */
    uint8_t             bSig1;
    /** Signature byte 2. */
    uint8_t             bSig2;
    /** Length of the entry (including the header). */
    uint8_t             cbEntry;
    /** Entry version number. */
    uint8_t             bVersion;
} ISO9660SUSPHDR;
AssertCompileSize(ISO9660SUSPHDR, 4);
/** Pointer to a SUSP header. */
typedef ISO9660SUSPHDR *PISO9660SUSPHDR;
/** Pointer to a const SUSP header. */
typedef ISO9660SUSPHDR const *PCISO9660SUSPHDR;


/**
 * SUSP continuation entry (CE).
 */
typedef struct ISO9660SUSPCE
{
    /** Header (ISO9660SUSPCE_SIG1, ISO9660SUSPCE_SIG2, ISO9660SUSPCE_VER). */
    ISO9660SUSPHDR      Hdr;
    /** The offset of the continutation data block (block offset).  */
    ISO9660U32          offBlock;
    /** The byte offset in the block of the contiuation data.  */
    ISO9660U32          offData;
    /** The size of the continuation data. */
    ISO9660U32          cbData;
} ISO9660SUSPCE;
/** Pointer to a SUSP continuation entry. */
typedef ISO9660SUSPCE *PISO9660SUSPCE;
/** Pointer to a const SUSP continuation entry. */
typedef ISO9660SUSPCE const *PCISO9660SUSPCE;
#define ISO9660SUSPCE_SIG1     'C' /**< SUSP continutation entry signature byte 1. */
#define ISO9660SUSPCE_SIG2     'E' /**< SUSP continutation entry signature byte 2. */
#define ISO9660SUSPCE_LEN      28  /**< SUSP continutation entry length. */
#define ISO9660SUSPCE_VER       1  /**< SUSP continutation entry version number. */
AssertCompileSize(ISO9660SUSPCE, ISO9660SUSPCE_LEN);


/**
 * SUSP padding entry (PD).
 */
typedef struct ISO9660SUSPPD
{
    /** Header (ISO9660SUSPPD_SIG1, ISO9660SUSPPD_SIG2, ISO9660SUSPPD_VER). */
    ISO9660SUSPHDR      Hdr;
    /* Padding follows. */
} ISO9660SUSPPD;
AssertCompileSize(ISO9660SUSPPD, 4);
/** Pointer to a SUSP padding entry. */
typedef ISO9660SUSPPD *PISO9660SUSPPD;
/** Pointer to a const SUSP padding entry. */
typedef ISO9660SUSPPD const *PCISO9660SUSPPD;
#define ISO9660SUSPPD_SIG1     'P' /**< SUSP padding entry signature byte 1. */
#define ISO9660SUSPPD_SIG2     'D' /**< SUSP padding entry signature byte 2. */
#define ISO9660SUSPPD_VER       1  /**< SUSP padding entry version number. */


/**
 * SUSP system use protocol entry (SP)
 *
 * This is only used in the '.' record of the root directory.
 */
typedef struct ISO9660SUSPSP
{
    /** Header (ISO9660SUSPSP_SIG1, ISO9660SUSPSP_SIG2,
     *  ISO9660SUSPSP_LEN, ISO9660SUSPSP_VER). */
    ISO9660SUSPHDR      Hdr;
    /** Check byte 1 (ISO9660SUSPSP_CHECK1). */
    uint8_t             bCheck1;
    /** Check byte 2 (ISO9660SUSPSP_CHECK2). */
    uint8_t             bCheck2;
    /** Number of bytes to skip within the system use field of each directory
     * entry (except the '.' entry of the root, since that's where this is). */
    uint8_t             cbSkip;
} ISO9660SUSPSP;
/** Pointer to a SUSP entry. */
typedef ISO9660SUSPSP *PISO9660SUSPSP;
/** Pointer to a const SUSP entry. */
typedef ISO9660SUSPSP const *PCISO9660SUSPSP;
#define ISO9660SUSPSP_SIG1     'S'              /**< SUSP system use protocol entry signature byte 1. */
#define ISO9660SUSPSP_SIG2     'P'              /**< SUSP system use protocol entry signature byte 2. */
#define ISO9660SUSPSP_VER       1               /**< SUSP system use protocol entry version number. */
#define ISO9660SUSPSP_LEN       7               /**< SUSP system use protocol entry length (fixed). */
#define ISO9660SUSPSP_CHECK1    UINT8_C(0xbe)   /**< SUSP system use protocol entry check byte 1. */
#define ISO9660SUSPSP_CHECK2    UINT8_C(0xef)   /**< SUSP system use protocol entry check byte 2. */
AssertCompileSize(ISO9660SUSPSP, ISO9660SUSPSP_LEN);


/**
 * SUSP terminator entry (ST)
 *
 * Used to terminate system use entries.
 */
typedef struct ISO9660SUSPST
{
    /** Header (ISO9660SUSPST_SIG1, ISO9660SUSPST_SIG2,
     *  ISO9660SUSPST_LEN, ISO9660SUSPST_VER). */
    ISO9660SUSPHDR      Hdr;
} ISO9660SUSPST;
/** Pointer to a SUSP padding entry. */
typedef ISO9660SUSPST *PISO9660SUSPST;
/** Pointer to a const SUSP padding entry. */
typedef ISO9660SUSPST const *PCISO9660SUSPST;
#define ISO9660SUSPST_SIG1     'S'             /**< SUSP system use protocol entry signature byte 1. */
#define ISO9660SUSPST_SIG2     'T'             /**< SUSP system use protocol entry signature byte 2. */
#define ISO9660SUSPST_VER       1              /**< SUSP system use protocol entry version number. */
#define ISO9660SUSPST_LEN       4              /**< SUSP system use protocol entry length (fixed). */
AssertCompileSize(ISO9660SUSPST, ISO9660SUSPST_LEN);


/**
 * SUSP extension record entry (ER)
 *
 * This is only used in the '.' record of the root directory.  There can be multiple of these.
 */
typedef struct ISO9660SUSPER
{
    /** Header (ISO9660SUSPER_SIG1, ISO9660SUSPER_SIG2, ISO9660SUSPER_VER). */
    ISO9660SUSPHDR      Hdr;
    /** The length of the identifier component. */
    uint8_t             cchIdentifier;
    /** The length of the description component. */
    uint8_t             cchDescription;
    /** The length of the source component.   */
    uint8_t             cchSource;
    /** The extension version number. */
    uint8_t             bVersion;
    /** The payload: first @a cchIdentifier chars of identifier string, second
     * @a cchDescription chars of description string, thrid @a cchSource chars
     * of source string.  Variable length. */
    char                achPayload[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} ISO9660SUSPER;
/** Pointer to a SUSP padding entry. */
typedef ISO9660SUSPER *PISO9660SUSPER;
/** Pointer to a const SUSP padding entry. */
typedef ISO9660SUSPER const *PCISO9660SUSPER;
#define ISO9660SUSPER_SIG1     'E'             /**< SUSP extension record entry signature byte 1. */
#define ISO9660SUSPER_SIG2     'R'             /**< SUSP extension record entry signature byte 2. */
#define ISO9660SUSPER_VER       1              /**< SUSP extension record entry version number. */
#define ISO9660SUSPER_OFF_PAYLOAD 8            /**< SUSP extension record entry payload member offset. */
AssertCompileMemberOffset(ISO9660SUSPER, achPayload, ISO9660SUSPER_OFF_PAYLOAD);

/**
 * SUSP extension sequence entry (ES)
 *
 * This is only used in the '.' record of the root directory.
 */
typedef struct ISO9660SUSPES
{
    /** Header (ISO9660SUSPES_SIG1, ISO9660SUSPES_SIG2, ISO9660SUSPES_VER). */
    ISO9660SUSPHDR      Hdr;
    /** The ER entry sequence number of the extension comming first. */
    uint8_t             iFirstExtension;
} ISO9660SUSPES;
/** Pointer to a SUSP padding entry. */
typedef ISO9660SUSPES *PISO9660SUSPES;
/** Pointer to a const SUSP padding entry. */
typedef ISO9660SUSPES const *PCISO9660SUSPES;
#define ISO9660SUSPES_SIG1     'E'             /**< SUSP extension sequence entry signature byte 1. */
#define ISO9660SUSPES_SIG2     'S'             /**< SUSP extension sequence entry signature byte 2. */
#define ISO9660SUSPES_VER       1              /**< SUSP extension sequence entry version number. */
#define ISO9660SUSPES_LEN       5              /**< SUSP extension sequence entry length (fixed). */
AssertCompileSize(ISO9660SUSPES, ISO9660SUSPES_LEN);


/** RRIP ER identifier string from Rock Ridge Interchange Protocol v1.10 specs. */
#define ISO9660_RRIP_ID         "RRIP_1991A"
/** RRIP ER recommended description string (from RRIP v1.10 specs). */
#define ISO9660_RRIP_DESC       "THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX FILE SYSTEM SEMANTICS"
/** RRIP ER recommended source string  (from RRIP v1.10 specs). */
#define ISO9660_RRIP_SRC        "PLEASE CONTACT DISC PUBLISHER FOR SPECIFICATION SOURCE.  SEE PUBLISHER IDENTIFIER IN PRIMARY VOLUME DESCRIPTOR FOR CONTACT INFORMATION."
/** RRIP ER version field value from the Rock Ridge Interchange Protocol v1.10 specs. */
#define ISO9660_RRIP_VER         1
/** The length of a RRIP v1.10 ER record.
 * The record must be constructed using ISO9660_RRIP_ID, ISO9660_RRIP_DESC
 * and ISO9660_RRIP_SRC. */
#define ISO9660_RRIP_ER_LEN     ((uint8_t)(  ISO9660SUSPER_OFF_PAYLOAD \
                                           + sizeof(ISO9660_RRIP_ID)   - 1 \
                                           + sizeof(ISO9660_RRIP_DESC) - 1 \
                                           + sizeof(ISO9660_RRIP_SRC)  - 1 ))

/** RRIP ER identifier string from RRIP IEEE P1282 v1.12 draft. */
#define ISO9660_RRIP_1_12_ID    "IEEE_P1282"
/** RRIP ER recommended description string (RRIP IEEE P1282 v1.12 draft). */
#define ISO9660_RRIP_1_12_DESC  "THE IEEE P1282 PROTOCOL PROVIDES SUPPORT FOR POSIX FILE SYSTEM SEMANTICS."
/** RRIP ER recommended source string  (RRIP IEEE P1282 v1.12 draft). */
#define ISO9660_RRIP_1_12_SRC   "PLEASE CONTACT THE IEEE STANDARDS DEPARTMENT, PISCATAWAY, NJ, USA FOR THE P1282 SPECIFICATION."
/** RRIP ER version field value from the Rock Ridge Interchange Protocol v1.12 specs. */
#define ISO9660_RRIP_1_12_VER   1
/** The length of a RRIP v1.12 ER record.
 * The record must be constructed using ISO9660_RRIP_1_12_ID,
 * ISO9660_RRIP_1_12_DESC and ISO9660_RRIP_1_12_SRC. */
#define ISO9660_RRIP_1_12_ER_LEN ((uint8_t)(  ISO9660SUSPER_OFF_PAYLOAD \
                                            + sizeof(ISO9660_RRIP_1_12_ID)   - 1 \
                                            + sizeof(ISO9660_RRIP_1_12_DESC) - 1 \
                                            + sizeof(ISO9660_RRIP_1_12_SRC)  - 1 ))


/**
 * Rock ridge interchange protocol -  RR.
 */
typedef struct ISO9660RRIPRR
{
    /** Header (ISO9660RRIPRR_SIG1, ISO9660RRIPRR_SIG2,
     *  ISO9660RRIPRR_LEN, ISO9660RRIPRR_VER). */
    ISO9660SUSPHDR      Hdr;
    /** Flags indicating which RRIP entries are present (). */
    uint8_t             fFlags;
} ISO9660RRIPRR;
/** Pointer to a RRIP RR entry. */
typedef ISO9660RRIPRR *PISO9660RRIPRR;
/** Pointer to a const RRIP RR entry. */
typedef ISO9660RRIPRR const *PCISO9660RRIPRR;
#define ISO9660RRIPRR_SIG1     'R'             /**< RRIP RR entry signature byte 1. */
#define ISO9660RRIPRR_SIG2     'R'             /**< RRIP RR entry signature byte 2. */
#define ISO9660RRIPRR_VER       1              /**< RRIP RR entry version number. */
#define ISO9660RRIPRR_LEN       5              /**< RRIP RR entry length (fixed). */
AssertCompileSize(ISO9660RRIPRR, ISO9660RRIPRR_LEN);

/** @name ISO9660RRIP_RR_F_XXX - Indicates which RRIP entries are present.
 * @{ */
#define ISO9660RRIP_RR_F_PX     UINT8_C(0x01)
#define ISO9660RRIP_RR_F_PN     UINT8_C(0x02)
#define ISO9660RRIP_RR_F_SL     UINT8_C(0x04)
#define ISO9660RRIP_RR_F_NM     UINT8_C(0x08)
#define ISO9660RRIP_RR_F_CL     UINT8_C(0x10)
#define ISO9660RRIP_RR_F_PL     UINT8_C(0x20)
#define ISO9660RRIP_RR_F_RE     UINT8_C(0x40)
#define ISO9660RRIP_RR_F_TF     UINT8_C(0x80)
/** @} */

/**
 * Rock ridge interchange protocol -  posix attribute entry (PX).
 */
typedef struct ISO9660RRIPPX
{
    /** Header (ISO9660RRIPPX_SIG1, ISO9660RRIPPX_SIG2,
     *  ISO9660RRIPPX_LEN, ISO9660RRIPPX_VER). */
    ISO9660SUSPHDR      Hdr;
    /** The file mode (RTFS_UNIX_XXX, RTFS_TYPE_XXX). */
    ISO9660U32          fMode;
    /** Number of hardlinks. */
    ISO9660U32          cHardlinks;
    /** User ID. */
    ISO9660U32          uid;
    /** Group ID. */
    ISO9660U32          gid;
    /** Inode number. */
    ISO9660U32          INode;
} ISO9660RRIPPX;
/** Pointer to a RRIP posix attribute entry. */
typedef ISO9660RRIPPX *PISO9660RRIPPX;
/** Pointer to a const RRIP posix attribute entry. */
typedef ISO9660RRIPPX const *PCISO9660RRIPPX;
#define ISO9660RRIPPX_SIG1     'P'             /**< RRIP posix attribute entry signature byte 1. */
#define ISO9660RRIPPX_SIG2     'X'             /**< RRIP posix attribute entry signature byte 2. */
#define ISO9660RRIPPX_VER       1              /**< RRIP posix attribute entry version number. */
#define ISO9660RRIPPX_LEN       44             /**< RRIP posix attribute entry length (fixed). */
AssertCompileSize(ISO9660RRIPPX, ISO9660RRIPPX_LEN);
#define ISO9660RRIPPX_LEN_NO_INODE 36          /**< RRIP posix attribute entry length without inode (fixed). */


/**
 * Rock ridge interchange protocol -  timestamp entry (TF).
 */
typedef struct ISO9660RRIPTF
{
    /** Header (ISO9660RRIPTF_SIG1, ISO9660RRIPTF_SIG2, ISO9660RRIPTF_VER). */
    ISO9660SUSPHDR      Hdr;
    /** Flags, ISO9660RRIPTF_F_XXX. */
    uint8_t             fFlags;
    /** Timestamp payload bytes (variable size and format). */
    uint8_t             abPayload[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} ISO9660RRIPTF;
AssertCompileMemberOffset(ISO9660RRIPTF, abPayload, 5);
/** Pointer to a RRIP timestamp  entry. */
typedef ISO9660RRIPTF *PISO9660RRIPTF;
/** Pointer to a const RRIP timestamp entry. */
typedef ISO9660RRIPTF const *PCISO9660RRIPTF;
#define ISO9660RRIPTF_SIG1     'T'             /**< RRIP child link entry signature byte 1. */
#define ISO9660RRIPTF_SIG2     'F'             /**< RRIP child link entry signature byte 2. */
#define ISO9660RRIPTF_VER       1              /**< RRIP child link entry version number. */

/** @name ISO9660RRIPTF_F_XXX - Timestmap flags.
 * @{ */
#define ISO9660RRIPTF_F_BIRTH           UINT8_C(0x01) /**< Birth (creation) timestamp is recorded. */
#define ISO9660RRIPTF_F_MODIFY          UINT8_C(0x02) /**< Modification timestamp is recorded. */
#define ISO9660RRIPTF_F_ACCESS          UINT8_C(0x04) /**< Accessed timestamp is recorded. */
#define ISO9660RRIPTF_F_CHANGE          UINT8_C(0x08) /**< Attribute change timestamp is recorded. */
#define ISO9660RRIPTF_F_BACKUP          UINT8_C(0x10) /**< Backup timestamp is recorded. */
#define ISO9660RRIPTF_F_EXPIRATION      UINT8_C(0x20) /**< Expiration timestamp is recorded. */
#define ISO9660RRIPTF_F_EFFECTIVE       UINT8_C(0x40) /**< Effective timestamp is recorded. */
#define ISO9660RRIPTF_F_LONG_FORM       UINT8_C(0x80) /**< If set ISO9660TIMESTAMP is used, otherwise ISO9660RECTIMESTAMP. */
/** @} */

/**
 * Calculates the length of a 'TF' entry given the flags.
 *
 * @returns Length in bytes.
 * @param   fFlags              The flags (ISO9660RRIPTF_F_XXX).
 */
DECLINLINE(uint8_t) Iso9660RripTfCalcLength(uint8_t fFlags)
{
    unsigned cTimestamps = ((fFlags & ISO9660RRIPTF_F_BIRTH)      != 0)
                         + ((fFlags & ISO9660RRIPTF_F_MODIFY)     != 0)
                         + ((fFlags & ISO9660RRIPTF_F_ACCESS)     != 0)
                         + ((fFlags & ISO9660RRIPTF_F_CHANGE)     != 0)
                         + ((fFlags & ISO9660RRIPTF_F_BACKUP)     != 0)
                         + ((fFlags & ISO9660RRIPTF_F_EXPIRATION) != 0)
                         + ((fFlags & ISO9660RRIPTF_F_EFFECTIVE)  != 0);
    return (uint8_t)(  cTimestamps * (fFlags & ISO9660RRIPTF_F_LONG_FORM ? sizeof(ISO9660TIMESTAMP) : sizeof(ISO9660RECTIMESTAMP))
                     + RT_OFFSETOF(ISO9660RRIPTF, abPayload));
}


/**
 * Rock ridge interchange protocol -  posix device number entry (PN).
 *
 * Mandatory for block or character devices.
 */
typedef struct ISO9660RRIPPN
{
    /** Header (ISO9660RRIPPN_SIG1, ISO9660RRIPPN_SIG2,
     *  ISO9660RRIPPN_LEN, ISO9660RRIPPN_VER). */
    ISO9660SUSPHDR      Hdr;
    /** The major device number. */
    ISO9660U32          Major;
    /** The minor device number. */
    ISO9660U32          Minor;
} ISO9660RRIPPN;
/** Pointer to a RRIP posix attribute entry. */
typedef ISO9660RRIPPN *PISO9660RRIPPN;
/** Pointer to a const RRIP posix attribute entry. */
typedef ISO9660RRIPPN const *PCISO9660RRIPPN;
#define ISO9660RRIPPN_SIG1     'P'             /**< RRIP posix device number entry signature byte 1. */
#define ISO9660RRIPPN_SIG2     'N'             /**< RRIP posix device number entry signature byte 2. */
#define ISO9660RRIPPN_VER       1              /**< RRIP posix device number entry version number. */
#define ISO9660RRIPPN_LEN       20             /**< RRIP posix device number entry length (fixed). */
AssertCompileSize(ISO9660RRIPPN, ISO9660RRIPPN_LEN);

/**
 * Rock ridge interchange protocol -  symlink entry (SL).
 *
 * Mandatory for symbolic links.
 */
typedef struct ISO9660RRIPSL
{
    /** Header (ISO9660RRIPSL_SIG1, ISO9660RRIPSL_SIG2, ISO9660RRIPSL_VER). */
    ISO9660SUSPHDR      Hdr;
    /** Flags (0 or ISO9660RRIP_SL_F_CONTINUE). */
    uint8_t             fFlags;
    /** Variable length of components.  First byte in each component is a
     *  combination of ISO9660RRIP_SL_C_XXX flag values.  The second byte the
     *  length of character data following it. */
    uint8_t             abComponents[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} ISO9660RRIPSL;
AssertCompileMemberOffset(ISO9660RRIPSL, abComponents, 5);
/** Pointer to a RRIP symbolic link entry. */
typedef ISO9660RRIPSL *PISO9660RRIPSL;
/** Pointer to a const RRIP symbolic link entry. */
typedef ISO9660RRIPSL const *PCISO9660RRIPSL;
#define ISO9660RRIPSL_SIG1     'S'             /**< RRIP symbolic link entry signature byte 1. */
#define ISO9660RRIPSL_SIG2     'L'             /**< RRIP symbolic link entry signature byte 2. */
#define ISO9660RRIPSL_VER       1              /**< RRIP symbolic link entry version number. */
/** ISO9660RRIPSL.fFlags - When set another symlink entry follows this one. */
#define ISO9660RRIP_SL_F_CONTINUE       UINT8_C(0x01)
/** @name ISO9660RRIP_SL_C_XXX - Symlink component flags.
 * @note These matches ISO9660RRIP_NM_F_XXX.
 * @{ */
/** Indicates that the component continues in the next entry.   */
#define ISO9660RRIP_SL_C_CONTINUE       UINT8_C(0x01)
/** Refer to '.' (the current dir). */
#define ISO9660RRIP_SL_C_CURRENT        UINT8_C(0x02)
/** Refer to '..' (the parent dir). */
#define ISO9660RRIP_SL_C_PARENT         UINT8_C(0x04)
/** Refer to '/' (the root dir). */
#define ISO9660RRIP_SL_C_ROOT           UINT8_C(0x08)
/** Reserved / historically was mount point reference. */
#define ISO9660RRIP_SL_C_MOUNT_POINT    UINT8_C(0x10)
/** Reserved / historically was uname network node name. */
#define ISO9660RRIP_SL_C_UNAME          UINT8_C(0x20)
/** Reserved mask (considers historically bits reserved). */
#define ISO9660RRIP_SL_C_RESERVED_MASK  UINT8_C(0xf0)
/** @} */


/**
 * Rock ridge interchange protocol -  name entry (NM).
 */
typedef struct ISO9660RRIPNM
{
    /** Header (ISO9660RRIPNM_SIG1, ISO9660RRIPNM_SIG2, ISO9660RRIPNM_VER). */
    ISO9660SUSPHDR      Hdr;
    /** Flags (ISO9660RRIP_NM_F_XXX). */
    uint8_t             fFlags;
    /** The name part (if any).  */
    char                achName[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} ISO9660RRIPNM;
AssertCompileMemberOffset(ISO9660RRIPNM, achName, 5);
/** Pointer to a RRIP name entry. */
typedef ISO9660RRIPNM *PISO9660RRIPNM;
/** Pointer to a const RRIP name entry. */
typedef ISO9660RRIPNM const *PCISO9660RRIPNM;
#define ISO9660RRIPNM_SIG1     'N'             /**< RRIP name entry signature byte 1. */
#define ISO9660RRIPNM_SIG2     'M'             /**< RRIP name entry signature byte 2. */
#define ISO9660RRIPNM_VER       1              /**< RRIP name entry version number. */
/** @name ISO9660RRIP_NM_F_XXX - Name flags.
 * @note These matches ISO9660RRIP_SL_C_XXX.
 * @{ */
/** Indicates there are more 'NM' entries.   */
#define ISO9660RRIP_NM_F_CONTINUE       UINT8_C(0x01)
/** Refer to '.' (the current dir). */
#define ISO9660RRIP_NM_F_CURRENT        UINT8_C(0x02)
/** Refer to '..' (the parent dir). */
#define ISO9660RRIP_NM_F_PARENT         UINT8_C(0x04)
/** Reserved / historically was uname network node name. */
#define ISO9660RRIP_NM_F_UNAME          UINT8_C(0x20)
/** Reserved mask (considers historical bits reserved). */
#define ISO9660RRIP_NM_F_RESERVED_MASK  UINT8_C(0xf8)
/** @} */

/** Maximum name length in one 'NM' entry. */
#define ISO9660RRIPNM_MAX_NAME_LEN      250


/**
 * Rock ridge interchange protocol -  child link entry (CL).
 *
 * This is used for relocated directories.  Relocated directries are employed
 * to bypass the ISO 9660 maximum tree depth of 8.
 *
 * The size of the directory and everything else is found in the '.' entry in
 * the specified location.  Only the name (NM or dir rec) and this link record
 * should be used.
 */
typedef struct ISO9660RRIPCL
{
    /** Header (ISO9660RRIPCL_SIG1, ISO9660RRIPCL_SIG2,
     *  ISO9660RRIPCL_LEN, ISO9660RRIPCL_VER). */
    ISO9660SUSPHDR      Hdr;
    /** The offset of the directory data (block offset). */
    ISO9660U32          offExtend;
} ISO9660RRIPCL;
/** Pointer to a RRIP child link entry. */
typedef ISO9660RRIPCL *PISO9660RRIPCL;
/** Pointer to a const RRIP child link entry. */
typedef ISO9660RRIPCL const *PCISO9660RRIPCL;
#define ISO9660RRIPCL_SIG1     'C'             /**< RRIP child link entry signature byte 1. */
#define ISO9660RRIPCL_SIG2     'L'             /**< RRIP child link entry signature byte 2. */
#define ISO9660RRIPCL_VER       1              /**< RRIP child link entry version number. */
#define ISO9660RRIPCL_LEN       12             /**< RRIP child link entry length. */
AssertCompileSize(ISO9660RRIPCL, ISO9660RRIPCL_LEN);


/**
 * Rock ridge interchange protocol -  parent link entry (PL).
 *
 * This is used in relocated directories.  Relocated directries are employed
 * to bypass the ISO 9660 maximum tree depth of 8.
 *
 * The size of the directory and everything else is found in the '.' entry in
 * the specified location.  Only the name (NM or dir rec) and this link record
 * should be used.
 */
typedef struct ISO9660RRIPPL
{
    /** Header (ISO9660RRIPPL_SIG1, ISO9660RRIPPL_SIG2,
     *  ISO9660RRIPPL_LEN, ISO9660RRIPPL_VER). */
    ISO9660SUSPHDR      Hdr;
    /** The offset of the directory data (block offset). */
    ISO9660U32          offExtend;
} ISO9660RRIPPL;
/** Pointer to a RRIP parent link entry. */
typedef ISO9660RRIPPL *PISO9660RRIPPL;
/** Pointer to a const RRIP parent link entry. */
typedef ISO9660RRIPPL const *PCISO9660RRIPPL;
#define ISO9660RRIPPL_SIG1     'P'             /**< RRIP parent link entry signature byte 1. */
#define ISO9660RRIPPL_SIG2     'L'             /**< RRIP parent link entry signature byte 2. */
#define ISO9660RRIPPL_VER       1              /**< RRIP parent link entry version number. */
#define ISO9660RRIPPL_LEN       12             /**< RRIP parent link entry length. */
AssertCompileSize(ISO9660RRIPPL, ISO9660RRIPPL_LEN);


/**
 * Rock ridge interchange protocol -  relocated entry (RE).
 *
 * This is used in the directory record for a relocated directory in the
 * holding place high up in the directory hierarchy.  The system may choose to
 * ignore/hide entries with this entry present.
 */
typedef struct ISO9660RRIPRE
{
    /** Header (ISO9660RRIPRE_SIG1, ISO9660RRIPRE_SIG2,
     *  ISO9660RRIPRE_LEN, ISO9660RRIPRE_VER). */
    ISO9660SUSPHDR      Hdr;
} ISO9660RRIPRE;
/** Pointer to a RRIP parent link entry. */
typedef ISO9660RRIPRE *PISO9660RRIPRE;
/** Pointer to a const RRIP parent link entry. */
typedef ISO9660RRIPRE const *PCISO9660RRIPRE;
#define ISO9660RRIPRE_SIG1     'R'             /**< RRIP relocated entry signature byte 1. */
#define ISO9660RRIPRE_SIG2     'E'             /**< RRIP relocated entry signature byte 2. */
#define ISO9660RRIPRE_VER       1              /**< RRIP relocated entry version number. */
#define ISO9660RRIPRE_LEN       4              /**< RRIP relocated entry length. */
AssertCompileSize(ISO9660RRIPRE, ISO9660RRIPRE_LEN);


/**
 * Rock ridge interchange protocol -  sparse file entry (SF).
 */
#pragma pack(1)
typedef struct ISO9660RRIPSF
{
    /** Header (ISO9660RRIPSF_SIG1, ISO9660RRIPSF_SIG2,
     *  ISO9660RRIPSF_LEN, ISO9660RRIPSF_VER). */
    ISO9660SUSPHDR      Hdr;
    /** The high 32-bits of the 64-bit sparse file size. */
    ISO9660U32          cbSparseHi;
    /** The low 32-bits of the 64-bit sparse file size. */
    ISO9660U32          cbSparseLo;
    /** The table depth. */
    uint8_t             cDepth;
} ISO9660RRIPSF;
#pragma pack()
/** Pointer to a RRIP symbolic link entry. */
typedef ISO9660RRIPSF *PISO9660RRIPSF;
/** Pointer to a const RRIP symbolic link entry. */
typedef ISO9660RRIPSF const *PCISO9660RRIPSF;
#define ISO9660RRIPSF_SIG1     'S'             /**< RRIP spare file entry signature byte 1. */
#define ISO9660RRIPSF_SIG2     'F'             /**< RRIP spare file entry signature byte 2. */
#define ISO9660RRIPSF_VER       1              /**< RRIP spare file entry version number. */
#define ISO9660RRIPSF_LEN       21             /**< RRIP spare file entry length. */
AssertCompileSize(ISO9660RRIPSF, ISO9660RRIPSF_LEN);

/** @name ISO9660RRIP_SF_TAB_F_XXX - Sparse table format.
 * @{ */
/** The 24-bit logical block number mask.
 * This is somewhat complicated, see docs.  MBZ for EMPTY. */
#define ISO9660RRIP_SF_TAB_F_BLOCK_MASK     UINT32_C(0x00ffffff)
/** Reserved bits, MBZ. */
#define ISO9660RRIP_SF_TAB_F_RESERVED       RT_BIT_32()
/** References a sub-table with 256 entries (ISO9660U32). */
#define ISO9660RRIP_SF_TAB_F_TABLE          RT_BIT_32(30)
/** Zero data region. */
#define ISO9660RRIP_SF_TAB_F_EMPTY          RT_BIT_32(31)
/** @} */


/**
 * SUSP and RRIP union.
 */
typedef union ISO9660SUSPUNION
{
    ISO9660SUSPHDR  Hdr;    /**< SUSP header . */
    ISO9660SUSPCE   CE;     /**< SUSP continuation entry. */
    ISO9660SUSPPD   PD;     /**< SUSP padding entry. */
    ISO9660SUSPSP   SP;     /**< SUSP system use protocol entry. */
    ISO9660SUSPST   ST;     /**< SUSP terminator entry. */
    ISO9660SUSPER   ER;     /**< SUSP extension record entry. */
    ISO9660SUSPES   ES;     /**< SUSP extension sequence entry. */
    ISO9660RRIPRR   RR;     /**< RRIP optimization entry. */
    ISO9660RRIPPX   PX;     /**< RRIP posix attribute entry. */
    ISO9660RRIPTF   TF;     /**< RRIP timestamp entry. */
    ISO9660RRIPPN   PN;     /**< RRIP posix device number entry. */
    ISO9660RRIPSF   SF;     /**< RRIP sparse file entry. */
    ISO9660RRIPSL   SL;     /**< RRIP symbolic link entry. */
    ISO9660RRIPNM   NM;     /**< RRIP name entry. */
    ISO9660RRIPCL   CL;     /**< RRIP child link entry. */
    ISO9660RRIPPL   PL;     /**< RRIP parent link entry. */
    ISO9660RRIPRE   RE;     /**< RRIP relocated entry. */
} ISO9660SUSPUNION;
/** Pointer to a SUSP and RRIP union. */
typedef ISO9660SUSPUNION *PISO9660SUSPUNION;
/** Pointer to a const SUSP and RRIP union. */
typedef ISO9660SUSPUNION *PCISO9660SUSPUNION;


/** @} */

#endif /* !IPRT_INCLUDED_formats_iso9660_h */

