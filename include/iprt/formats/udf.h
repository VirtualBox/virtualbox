/* $Id: udf.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, Universal Disk Format (UDF).
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

#ifndef IPRT_INCLUDED_formats_udf_h
#define IPRT_INCLUDED_formats_udf_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>
#include <iprt/formats/iso9660.h>


/** @defgroup grp_rt_formats_udf    Universal Disk Format (UDF) structures and definitions
 * @ingroup grp_rt_formats
 *
 * References:
 *  - https://www.ecma-international.org/publications/files/ECMA-ST/Ecma-167.pdf
 *  - http://www.osta.org/specs/pdf/udf260.pdf
 *  - http://wiki.osdev.org/UDF
 *  - https://sites.google.com/site/udfintro/
 *
 * @{
 */

/**
 * UDF d-character string (@ecma167{1,7.2.12,25}).
 *
 * This is mainly to mark what's d-strings and what's not.
 */
typedef char UDFDSTRING;
/** Pointer to an UDF dstring. */
typedef UDFDSTRING *PUDFDSTRING;
/** Pointer to a const UDF dstring. */
typedef UDFDSTRING const *PCUDFDSTRING;

/**
 * UDF extent allocation descriptor (AD) (@ecma167{3,7.1,42}).
 */
typedef struct UDFEXTENTAD
{
    /** Extent length in bytes. */
    uint32_t        cb;
    /** Extent offset (logical sector number).
     * If @a cb is zero, this is also zero. */
    uint32_t        off;
} UDFEXTENTAD;
AssertCompileSize(UDFEXTENTAD, 8);
/** Pointer to an UDF extent descriptor. */
typedef UDFEXTENTAD *PUDFEXTENTAD;
/** Pointer to a const UDF extent descriptor. */
typedef UDFEXTENTAD const *PCUDFEXTENTAD;


/**
 * UDF logical block address (@ecma167{4,7.1,73}).
 */
#pragma pack(2)
typedef struct UDFLBADDR
{
    /** Logical block number, relative to the start of the given partition. */
    uint32_t        off;
    /** Partition reference number. */
    uint16_t        uPartitionNo;
} UDFLBADDR;
#pragma pack()
AssertCompileSize(UDFLBADDR, 6);
/** Pointer to an UDF logical block address. */
typedef UDFLBADDR *PUDFLBADDR;
/** Pointer to a const UDF logical block address. */
typedef UDFLBADDR const *PCUDFLBADDR;


/** @name UDF_AD_TYPE_XXX - Allocation descriptor types.
 *
 * Used by UDFSHORTAD::uType, UDFLONGAD::uType and UDFEXTAD::uType.
 *
 * See @ecma167{4,14.14.1.1,116}.
 *
 * @{ */
/** Recorded and allocated.
 * Also used for zero length descriptors. */
#define UDF_AD_TYPE_RECORDED_AND_ALLOCATED          0
/** Allocated but not recorded. */
#define UDF_AD_TYPE_ONLY_ALLOCATED                  1
/** Not recorded nor allocated. */
#define UDF_AD_TYPE_FREE                            2
/** Go figure. */
#define UDF_AD_TYPE_NEXT                            3
/** @} */

/**
 * UDF short allocation descriptor (@ecma167{4,14.14.1,116}).
 */
typedef struct UDFSHORTAD
{
#ifdef RT_BIG_ENDIAN
    /** Extent type (UDF_AD_TYPE_XXX). */
    uint32_t        uType : 2;
    /** Extent length in bytes, top 2 bits . */
    uint32_t        cb : 30;
#else
    /** Extent length in bytes. */
    uint32_t        cb : 30;
    /** Extent type (UDF_AD_TYPE_XXX). */
    uint32_t        uType : 2;
#endif
    /** Extent offset (logical sector number). */
    uint32_t        off;
} UDFSHORTAD;
AssertCompileSize(UDFSHORTAD, 8);
/** Pointer to an UDF short allocation descriptor. */
typedef UDFSHORTAD *PUDFSHORTAD;
/** Pointer to a const UDF short allocation descriptor. */
typedef UDFSHORTAD const *PCUDFSHORTAD;

/**
 * UDF long allocation descriptor (@ecma167{4,14.14.2,116}).
 */
#pragma pack(2)
typedef struct UDFLONGAD
{
#ifdef RT_BIG_ENDIAN
    /** Extent type (UDF_AD_TYPE_XXX). */
    uint32_t        uType : 2;
    /** Extent length in bytes, top 2 bits . */
    uint32_t        cb : 30;
#else
    /** Extent length in bytes. */
    uint32_t        cb : 30;
    /** Extent type (UDF_AD_TYPE_XXX). */
    uint32_t        uType : 2;
#endif
    /** Extent location. */
    UDFLBADDR       Location;
    /** Implementation use area. */
    union
    {
        /** Generic view. */
        uint8_t     ab[6];
        /** Used in FIDs.
         * See @udf260{2.3.10.1,66}, @udf260{2.3.4.3,58}.
         */
        struct
        {
            /** Flags (UDF_AD_IMP_USE_FLAGS_XXX).   */
            uint16_t    fFlags;
            /** Unique ID. */
            uint32_t    idUnique;
        } Fid;
    } ImplementationUse;
} UDFLONGAD;
#pragma pack()
AssertCompileSize(UDFLONGAD, 16);
/** Pointer to an UDF long allocation descriptor. */
typedef UDFLONGAD *PUDFLONGAD;
/** Pointer to a const UDF long allocation descriptor. */
typedef UDFLONGAD const *PCUDFLONGAD;

/** @name UDF_AD_IMP_USE_FLAGS_XXX - UDFLONGAD::ImplementationUse::Fid::fFlags values
 * See @udf260{2.3.10.1,66}.
 * @{ */
/** Set if erased and the extend is of the type UDF_AD_TYPE_ONLY_ALLOCATED. */
#define UDF_AD_IMP_USE_FLAGS_ERASED         UINT16_C(0x0001)
/** Valid mask.   */
#define UDF_AD_IMP_USE_FLAGS_VALID_MASK     UINT16_C(0x0001)
/** @} */

/**
 * UDF extended allocation descriptor (@ecma167{4,14.14.3,117}).
 */
typedef struct UDFEXTAD
{
#ifdef RT_BIG_ENDIAN
    /** 0x00: Extent type (UDF_AD_TYPE_XXX). */
    uint32_t        uType : 2;
    /** 0x00: Extent length in bytes, top 2 bits . */
    uint32_t        cb : 30;
    /** 0x04: Reserved, MBZ. */
    uint32_t        uReserved : 2;
    /** 0x04: Number of bytes recorded. */
    uint32_t        cbRecorded : 30;
#else
    /** 0x00: Extent length in bytes. */
    uint32_t        cb : 30;
    /** 0x00: Extent type (UDF_AD_TYPE_XXX). */
    uint32_t        uType : 2;
    /** 0x04: Number of bytes recorded. */
    uint32_t        cbRecorded : 30;
    /** 0x04: Reserved, MBZ. */
    uint32_t        uReserved : 2;
#endif
    /** 0x08: Number of bytes of information (from first byte). */
    uint32_t        cbInformation;
    /** 0x0c: Extent location. */
    UDFLBADDR       Location;
    /** 0x12: Implementation use area. */
    uint8_t         abImplementationUse[2];
} UDFEXTAD;
AssertCompileSize(UDFEXTAD, 20);
/** Pointer to an UDF extended allocation descriptor. */
typedef UDFEXTAD *PUDFEXTAD;
/** Pointer to a const UDF extended allocation descriptor. */
typedef UDFEXTAD const *PCUDFEXTAD;


/**
 * UDF timestamp (@ecma167{1,7.3,25}, @udf260{2.1.4,19}).
 */
typedef struct UDFTIMESTAMP
{
#ifdef RT_BIG_ENDIAN
    /** 0x00: Type (UDFTIMESTAMP_T_XXX). */
    RT_GCC_EXTENSION uint16_t   fType : 4;
    /** 0x00: Time zone offset in minutes.
     * For EST this will be -300, whereas for CET it will be 60. */
    RT_GCC_EXTENSION int16_t    offUtcInMin : 12;
#else
    /** 0x00: Time zone offset in minutes.
     * For EST this will be -300, whereas for CET it will be 60. */
    RT_GCC_EXTENSION int16_t    offUtcInMin : 12;
    /** 0x00: Type (UDFTIMESTAMP_T_XXX). */
    RT_GCC_EXTENSION uint16_t   fType : 4;
#endif
    /** 0x02: The year. */
    int16_t         iYear;
    /** 0x04: Month of year (1-12). */
    uint8_t         uMonth;
    /** 0x05: Day of month (1-31). */
    uint8_t         uDay;
    /** 0x06: Hour of day (0-23). */
    uint8_t         uHour;
    /** 0x07: Minute of hour (0-59). */
    uint8_t         uMinute;
    /** 0x08: Second of minute (0-60 if type 2, otherwise 0-59). */
    uint8_t         uSecond;
    /** 0x09: Number of Centiseconds (0-99). */
    uint8_t         cCentiseconds;
    /** 0x0a: Number of hundreds of microseconds (0-99).  Unit is 100us. */
    uint8_t         cHundredsOfMicroseconds;
    /** 0x0b: Number of microseconds (0-99). */
    uint8_t         cMicroseconds;
} UDFTIMESTAMP;
AssertCompileSize(UDFTIMESTAMP, 12);
/** Pointer to an UDF timestamp. */
typedef UDFTIMESTAMP *PUDFTIMESTAMP;
/** Pointer to a const UDF timestamp. */
typedef UDFTIMESTAMP const *PCUDFTIMESTAMP;

/** @name UDFTIMESTAMP_T_XXX
 * @{ */
/** Local time. */
#define UDFTIMESTAMP_T_LOCAL                1
/** @} */

/** No time zone specified. */
#define UDFTIMESTAMP_NO_TIME_ZONE           (-2047)


/**
 * UDF character set specficiation (@ecma167{1,7.2.1,21}, @udf260{2.1.2,18}).
 */
typedef struct UDFCHARSPEC
{
    /** The character set type (UDF_CHAR_SET_TYPE_XXX) */
    uint8_t     uType;
    /** Character set information. */
    uint8_t     abInfo[63];
} UDFCHARSPEC;
AssertCompileSize(UDFCHARSPEC, 64);
/** Pointer to UDF character set specification. */
typedef UDFCHARSPEC *PUDFCHARSPEC;
/** Pointer to const UDF character set specification. */
typedef UDFCHARSPEC const *PCUDFCHARSPEC;

/** @name UDF_CHAR_SET_TYPE_XXX - Character set types.
 * @{ */
/** CS0: By agreement between the medium producer and consumer.
 * See UDF_CHAR_SET_OSTA_COMPRESSED_UNICODE. */
#define UDF_CHAR_SET_TYPE_BY_AGREEMENT  UINT8_C(0x00)
/** CS1: ASCII (ECMA-6) with all or part of the specified graphic characters. */
#define UDF_CHAR_SET_TYPE_ASCII         UINT8_C(0x01)
/** CS5: Latin-1 (ECMA-94) with all graphical characters. */
#define UDF_CHAR_SET_TYPE_LATIN_1       UINT8_C(0x05)
/* there are more defined here, but they are mostly useless, since UDF only uses CS0. */

/** The CS0 definition used by the UDF specification. */
#define UDF_CHAR_SET_OSTA_COMPRESSED_UNICODE        UDF_CHAR_SET_TYPE_BY_AGREEMENT
/** String to put in the UDFCHARSEPC::abInfo field for UDF CS0. */
#define UDF_CHAR_SET_OSTA_COMPRESSED_UNICODE_INFO   "OSTA Compressed Unicode"
/** @} */


/**
 * UDF entity identifier (@ecma167{1,7.4,26}, @udf260{2.1.5,20}).
 */
typedef struct UDFENTITYID
{
    /** 0x00: Flags (UDFENTITYID_FLAGS_XXX). */
    uint8_t         fFlags;
    /** 0x01: Identifier string (see UDF_ENTITY_ID_XXX). */
    char            achIdentifier[23];
    /** 0x18: Identifier suffix. */
    union
    {
        /** Domain ID suffix. */
        struct
        {
            uint16_t    uUdfRevision;
            uint8_t     fDomain;
            uint8_t     abReserved[5];
        } Domain;

        /** UDF ID suffix. */
        struct
        {
            uint16_t    uUdfRevision;
            uint8_t     bOsClass;
            uint8_t     idOS;
            uint8_t     abReserved[4];
        } Udf;


        /** Implementation ID suffix. */
        struct
        {
            uint8_t     bOsClass;
            uint8_t     idOS;
            uint8_t     achImplUse[6];
        } Implementation;

        /** Application ID suffix / generic. */
        uint8_t     abApplication[8];
    } Suffix;
} UDFENTITYID;
AssertCompileSize(UDFENTITYID, 32);
/** Pointer to UDF entity identifier. */
typedef UDFENTITYID *PUDFENTITYID;
/** Pointer to const UDF entity identifier. */
typedef UDFENTITYID const *PCUDFENTITYID;

/** @name UDF_ENTITY_ID_XXX - UDF identifier strings
 *
 * See @udf260{2.1.5.2,21}.
 *
 * @{ */
/** Implementation use volume descriptor, implementation ID field.
 * UDF ID suffix. */
#define UDF_ENTITY_ID_IUVD_IMPLEMENTATION       "*UDF LV Info"

/** Partition descriptor, partition contents field, set to indicate UDF
 * (ECMA-167 3rd edition). Application ID suffix. */
#define UDF_ENTITY_ID_PD_PARTITION_CONTENTS_UDF     "+NSR03"
/** Partition descriptor, partition contents field, set to indicate ISO-9660
 * (ECMA-119). Application ID suffix. */
#define UDF_ENTITY_ID_PD_PARTITION_CONTENTS_ISO9660 "+CD001"
/** Partition descriptor, partition contents field, set to indicate ECMA-168.
 * Application ID suffix. */
#define UDF_ENTITY_ID_PD_PARTITION_CONTENTS_CDW     "+CDW02"
/** Partition descriptor, partition contents field, set to indicate FAT
 * (ECMA-107). Application ID suffix. */
#define UDF_ENTITY_ID_PD_PARTITION_CONTENTS_FAT     "+FDC01"

/** Logical volume descriptor, domain ID field.
 * Domain ID suffix. */
#define UDF_ENTITY_ID_LVD_DOMAIN                "*OSTA UDF Compliant"

/** File set descriptor, domain ID field.
 * Domain ID suffix. */
#define UDF_ENTITY_FSD_LVD_DOMAIN               "*OSTA UDF Compliant"

/** UDF implementation use extended attribute, implementation ID field, set
 * to free EA space.  UDF ID suffix. */
#define UDF_ENTITY_ID_IUEA_FREE_EA_SPACE        "*UDF FreeEASpace"
/** UDF implementation use extended attribute, implementation ID field, set
 * to DVD copyright management information.  UDF ID suffix. */
#define UDF_ENTITY_ID_IUEA_DVD_CGMS_INFO        "*UDF DVD CGMS Info"
/** UDF implementation use extended attribute, implementation ID field, set
 * to OS/2 extended attribute length.  UDF ID suffix. */
#define UDF_ENTITY_ID_IUEA_OS2_EA_LENGTH        "*UDF OS/2 EALength"
/** UDF implementation use extended attribute, implementation ID field, set
 * to Machintosh OS volume information.  UDF ID suffix. */
#define UDF_ENTITY_ID_IUEA_MAC_VOLUME_INFO      "*UDF Mac VolumeInfo"
/** UDF implementation use extended attribute, implementation ID field, set
 * to Machintosh Finder Info.  UDF ID suffix. */
#define UDF_ENTITY_ID_IUEA_MAC_FINDER_INFO      "*UDF Mac FinderInfo"
/** UDF implementation use extended attribute, implementation ID field, set
 * to OS/400 extended directory information.  UDF ID suffix. */
#define UDF_ENTITY_ID_IUEA_OS400_DIR_INFO       "*UDF OS/400 DirInfo"

/** UDF application use extended attribute, application ID field, set
 * to free application use EA space.  UDF ID suffix. */
#define UDF_ENTITY_ID_AUEA_FREE_EA_SPACE        "*UDF FreeAppEASpace"

/** Virtual partition map, partition type field.
 * UDF ID suffix. */
#define UDF_ENTITY_ID_VPM_PARTITION_TYPE        "*UDF Virtual Partition"

/** Sparable partition map, partition type field.
 * UDF ID suffix. */
#define UDF_ENTITY_ID_SPM_PARTITION_TYPE        "*UDF Sparable Partition"

/** Metadata partition map, partition type field.
 * UDF ID suffix. */
#define UDF_ENTITY_ID_MPM_PARTITION_TYPE        "*UDF Metadata Partition"

/** Sparing table, sparing identifier field.
 * UDF ID suffix. */
#define UDF_ENTITY_ID_ST_SPARING                "*UDF Sparting Table"

/** @} */


/**
 * UDF descriptor tag (@ecma167{3,7.2,42}, @udf260{2.2.1,26}).
 */
typedef struct UDFTAG
{
    /** Tag identifier (UDF_TAG_ID_XXX). */
    uint16_t        idTag;
    /** Descriptor version. */
    uint16_t        uVersion;
    /** Tag checksum.
     * Sum of each byte in the structure with this field as zero.  */
    uint8_t         uChecksum;
    /** Reserved, MBZ. */
    uint8_t         bReserved;
    /** Tag serial number. */
    uint16_t        uTagSerialNo;
    /** Descriptor CRC. */
    uint16_t        uDescriptorCrc;
    /** Descriptor CRC length. */
    uint16_t        cbDescriptorCrc;
    /** The tag location (logical sector number). */
    uint32_t        offTag;
} UDFTAG;
AssertCompileSize(UDFTAG, 16);
/** Pointer to an UDF descriptor tag. */
typedef UDFTAG *PUDFTAG;
/** Pointer to a const UDF descriptor tag. */
typedef UDFTAG const *PCUDFTAG;

/** @name UDF_TAG_ID_XXX - UDF descriptor tag IDs.
 * @{ */
#define UDF_TAG_ID_PRIMARY_VOL_DESC                 UINT16_C(0x0001) /**< See UDFPRIMARYVOLUMEDESC */
#define UDF_TAG_ID_ANCHOR_VOLUME_DESC_PTR           UINT16_C(0x0002) /**< See UDFANCHORVOLUMEDESCPTR */
#define UDF_TAG_ID_VOLUME_DESC_PTR                  UINT16_C(0x0003) /**< See UDFVOLUMEDESCPTR */
#define UDF_TAG_ID_IMPLEMENTATION_USE_VOLUME_DESC   UINT16_C(0x0004) /**< See UDFIMPLEMENTATIONUSEVOLUMEDESC */
#define UDF_TAG_ID_PARTITION_DESC                   UINT16_C(0x0005) /**< See UDFPARTITIONDESC */
#define UDF_TAG_ID_LOGICAL_VOLUME_DESC              UINT16_C(0x0006) /**< See UDFLOGICALVOLUMEDESC */
#define UDF_TAG_ID_UNALLOCATED_SPACE_DESC           UINT16_C(0x0007) /**< See UDFUNALLOCATEDSPACEDESC */
#define UDF_TAG_ID_TERMINATING_DESC                 UINT16_C(0x0008) /**< See UDFTERMINATINGDESC */
#define UDF_TAG_ID_LOGICAL_VOLUME_INTEGRITY_DESC    UINT16_C(0x0009) /**< See UDFLOGICALVOLINTEGRITYDESC */
#define UDF_TAG_ID_FILE_SET_DESC                    UINT16_C(0x0100) /**< See UDFFILESETDESC */
#define UDF_TAG_ID_FILE_ID_DESC                     UINT16_C(0x0101) /**< See UDFFILEIDDESC */
#define UDF_TAG_ID_ALLOCATION_EXTENT_DESC           UINT16_C(0x0102) /**< See UDFALLOCATIONEXTENTDESC */
#define UDF_TAG_ID_INDIRECT_ENTRY                   UINT16_C(0x0103) /**< See UDFINDIRECTENTRY */
#define UDF_TAG_ID_TERMINAL_ENTRY                   UINT16_C(0x0104) /**< See UDFTERMINALENTRY */
#define UDF_TAG_ID_FILE_ENTRY                       UINT16_C(0x0105) /**< See UDFFILEENTRY */
#define UDF_TAG_ID_EXTENDED_ATTRIB_HDR_DESC         UINT16_C(0x0106) /**< See UDFEXTATTRIBHDRDESC */
#define UDF_TAG_ID_UNALLOCATED_SPACE_ENTRY          UINT16_C(0x0107) /**< See UDFUNALLOCATEDSPACEENTRY */
#define UDF_TAG_ID_SPACE_BITMAP_DESC                UINT16_C(0x0108) /**< See UDFSPACEBITMAPDESC */
#define UDF_TAG_ID_PARTITION_INTEGERITY_DESC        UINT16_C(0x0109) /**< See UDFPARTITIONINTEGRITYDESC */
#define UDF_TAG_ID_EXTENDED_FILE_ENTRY              UINT16_C(0x010a) /**< See UDFEXFILEENTRY */
/** @} */


/**
 * UDF primary volume descriptor (PVD) (@ecma167{3,10.1,50},
 * @udf260{2.2.2,27}).
 */
typedef struct UDFPRIMARYVOLUMEDESC
{
    /** 0x000: The descriptor tag (UDF_TAG_ID_PRIMARY_VOL_DESC). */
    UDFTAG          Tag;
    /** 0x010: Volume descriptor sequence number. */
    uint32_t        uVolumeDescSeqNo;
    /** 0x014: Primary volume descriptor number. */
    uint32_t        uPrimaryVolumeDescNo;
    /** 0x018: Volume identifier (dstring). */
    UDFDSTRING      achVolumeID[32];
    /** 0x038: Volume sequence number. */
    uint16_t        uVolumeSeqNo;
    /** 0x03a: Maximum volume sequence number. */
    uint16_t        uMaxVolumeSeqNo;
    /** 0x03c: Interchange level. */
    uint16_t        uInterchangeLevel;
    /** 0x03e: Maximum interchange level. */
    uint16_t        uMaxInterchangeLevel;
    /** 0x040: Character set bitmask (aka list).  Each bit correspond to a
     * character set number. */
    uint32_t        fCharacterSets;
    /** 0x044: Maximum character set bitmask (aka list). */
    uint32_t        fMaxCharacterSets;
    /** 0x048: Volume set identifier (dstring).  This starts with 16 unique
     *  characters, the first 8 being the hex representation of a time value. */
    UDFDSTRING      achVolumeSetID[128];
    /** 0x0c8: Descriptor character set.
     * For achVolumeSetID and achVolumeID. */
    UDFCHARSPEC     DescCharSet;
    /** 0x108: Explanatory character set.
     * For VolumeAbstract and VolumeCopyrightNotice data. */
    UDFCHARSPEC     ExplanatoryCharSet;
    /** 0x148: Volume abstract. */
    UDFEXTENTAD     VolumeAbstract;
    /** 0x150: Volume copyright notice. */
    UDFEXTENTAD     VolumeCopyrightNotice;
    /** 0x158: Application identifier ("*Application ID"). */
    UDFENTITYID     idApplication;
    /** 0x178: Recording date and time. */
    UDFTIMESTAMP    RecordingTimestamp;
    /** 0x184: Implementation identifier ("*Developer ID"). */
    UDFENTITYID     idImplementation;
    /** 0x1a4: Implementation use. */
    uint8_t         abImplementationUse[64];
    /** 0x1e4: Predecessor volume descriptor sequence location. */
    uint32_t        offPredecessorVolDescSeq;
    /** 0x1e8: Flags (UDF_PVD_FLAGS_XXX). */
    uint16_t        fFlags;
    /** 0x1ea: Reserved. */
    uint8_t         abReserved[22];
} UDFPRIMARYVOLUMEDESC;
AssertCompileSize(UDFPRIMARYVOLUMEDESC, 512);
/** Pointer to a UDF primary volume descriptor. */
typedef UDFPRIMARYVOLUMEDESC *PUDFPRIMARYVOLUMEDESC;
/** Pointer to a const UDF primary volume descriptor. */
typedef UDFPRIMARYVOLUMEDESC const *PCUDFPRIMARYVOLUMEDESC;

/** @name UDF_PVD_FLAGS_XXX - Flags for UDFPRIMARYVOLUMEDESC::fFlags.
 * @{ */
/** Indicates that the volume set ID is common to all members of the set.  */
#define UDF_PVD_FLAGS_COMMON_VOLUME_SET_ID      UINT16_C(0x0001)
/** @} */


/**
 * UDF anchor volume descriptor pointer (AVDP) (@ecma167{3,10.2,53},
 * @udf260{2.2.3,29}).
 *
 * This is stored at least two of these locations:
 *      - logical sector 256
 *      - logical sector N - 256.
 *      - logical sector N.
 */
typedef struct UDFANCHORVOLUMEDESCPTR
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_ANCHOR_VOLUME_DESC_PTR). */
    UDFTAG          Tag;
    /** 0x10: The extent describing the main volume descriptor sequence. */
    UDFEXTENTAD     MainVolumeDescSeq;
    /** 0x18: Location of the backup descriptor sequence. */
    UDFEXTENTAD     ReserveVolumeDescSeq;
    /** 0x20: Reserved, probably must be zeros. */
    uint8_t         abReserved[0x1e0];
} UDFANCHORVOLUMEDESCPTR;
AssertCompileSize(UDFANCHORVOLUMEDESCPTR, 512);
/** Pointer to UDF anchor volume descriptor pointer. */
typedef UDFANCHORVOLUMEDESCPTR *PUDFANCHORVOLUMEDESCPTR;
/** Pointer to const UDF anchor volume descriptor pointer. */
typedef UDFANCHORVOLUMEDESCPTR const *PCUDFANCHORVOLUMEDESCPTR;


/**
 * UDF volume descriptor pointer (VDP) (@ecma167{3,10.3,53}).
 */
typedef struct UDFVOLUMEDESCPTR
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_VOLUME_DESC_PTR). */
    UDFTAG          Tag;
    /** 0x10: Volume descriptor sequence number. */
    uint32_t        uVolumeDescSeqNo;
    /** 0x14: Location of the next volume descriptor sequence. */
    UDFEXTENTAD     NextVolumeDescSeq;
    /** 0x1c: Reserved, probably must be zeros. */
    uint8_t         abReserved[484];
} UDFVOLUMEDESCPTR;
AssertCompileSize(UDFVOLUMEDESCPTR, 512);
/** Pointer to UDF volume descriptor pointer. */
typedef UDFVOLUMEDESCPTR *PUDFVOLUMEDESCPTR;
/** Pointer to const UDF volume descriptor pointer. */
typedef UDFVOLUMEDESCPTR const *PCUDFVOLUMEDESCPTR;


/**
 * UDF implementation use volume descriptor (IUVD) (@ecma167{3,10.4,55},
 * @udf260{2.2.7,35}).
 */
typedef struct UDFIMPLEMENTATIONUSEVOLUMEDESC
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_IMPLEMENTATION_USE_VOLUME_DESC). */
    UDFTAG          Tag;
    /** 0x10: Volume descriptor sequence number. */
    uint32_t        uVolumeDescSeqNo;
    /** 0x14: The implementation identifier (UDF_ENTITY_ID_IUVD_IMPLEMENTATION). */
    UDFENTITYID     idImplementation;
    /** 0x34: The implementation use area. */
    union
    {
        /** Generic view. */
        uint8_t     ab[460];
        /** Logical volume information (@udf260{2.2.7.2,35}). */
        struct
        {
            /** 0x034: The character set used in this sub-structure. */
            UDFCHARSPEC     Charset;
            /** 0x074: Logical volume identifier. */
            UDFDSTRING      achVolumeID[128];
            /** 0x0f4: Info string \#1. */
            UDFDSTRING      achInfo1[36];
            /** 0x118: Info string \#2. */
            UDFDSTRING      achInfo2[36];
            /** 0x13c: Info string \#3. */
            UDFDSTRING      achInfo3[36];
            /** 0x160: The implementation identifier ("*Developer ID"). */
            UDFENTITYID     idImplementation;
            /** 0x180: Additional use bytes. */
            uint8_t         abUse[128];
        } Lvi;
    } ImplementationUse;
} UDFIMPLEMENTATIONUSEVOLUMEDESC;
AssertCompileSize(UDFIMPLEMENTATIONUSEVOLUMEDESC, 512);
AssertCompileMemberOffset(UDFIMPLEMENTATIONUSEVOLUMEDESC, ImplementationUse.Lvi.Charset,          0x034);
AssertCompileMemberOffset(UDFIMPLEMENTATIONUSEVOLUMEDESC, ImplementationUse.Lvi.achVolumeID,      0x074);
AssertCompileMemberOffset(UDFIMPLEMENTATIONUSEVOLUMEDESC, ImplementationUse.Lvi.achInfo1,         0x0f4);
AssertCompileMemberOffset(UDFIMPLEMENTATIONUSEVOLUMEDESC, ImplementationUse.Lvi.achInfo2,         0x118);
AssertCompileMemberOffset(UDFIMPLEMENTATIONUSEVOLUMEDESC, ImplementationUse.Lvi.achInfo3,         0x13c);
AssertCompileMemberOffset(UDFIMPLEMENTATIONUSEVOLUMEDESC, ImplementationUse.Lvi.idImplementation, 0x160);
/** Pointer to an UDF implementation use volume descriptor. */
typedef UDFIMPLEMENTATIONUSEVOLUMEDESC *PUDFIMPLEMENTATIONUSEVOLUMEDESC;
/** Pointer to a const UDF implementation use volume descriptor. */
typedef UDFIMPLEMENTATIONUSEVOLUMEDESC const *PCUDFIMPLEMENTATIONUSEVOLUMEDESC;


/**
 * UDF partition header descriptor (@ecma167{4,14.3,90}, @udf260{2.3.3,56}).
 *
 * This is found in UDFPARTITIONDESC::ContentsUse.
 */
typedef struct UDFPARTITIONHDRDESC
{
    /** 0x00: Unallocated space table location.  Zero length means no table. */
    UDFSHORTAD      UnallocatedSpaceTable;
    /** 0x08: Unallocated space bitmap location.  Zero length means no bitmap. */
    UDFSHORTAD      UnallocatedSpaceBitmap;
    /** 0x10: Partition integrity table location.  Zero length means no table. */
    UDFSHORTAD      PartitionIntegrityTable;
    /** 0x18: Freed space table location.  Zero length means no table. */
    UDFSHORTAD      FreedSpaceTable;
    /** 0x20: Freed space bitmap location.  Zero length means no bitmap. */
    UDFSHORTAD      FreedSpaceBitmap;
    /** 0x28: Reserved, MBZ. */
    uint8_t         abReserved[88];
} UDFPARTITIONHDRDESC;
AssertCompileSize(UDFPARTITIONHDRDESC, 128);
AssertCompileMemberOffset(UDFPARTITIONHDRDESC, PartitionIntegrityTable, 0x10);
AssertCompileMemberOffset(UDFPARTITIONHDRDESC, abReserved, 0x28);
/** Pointer to an UDF partition header descriptor. */
typedef UDFPARTITIONHDRDESC *PUDFPARTITIONHDRDESC;
/** Pointer to a const UDF partition header descriptor. */
typedef UDFPARTITIONHDRDESC const *PCUDFPARTITIONHDRDESC;


/**
 * UDF partition descriptor (PD) (@ecma167{3,10.5,55}, @udf260{2.2.14,51}).
 */
typedef struct UDFPARTITIONDESC
{
    /** 0x000: The descriptor tag (UDF_TAG_ID_PARTITION_DESC). */
    UDFTAG          Tag;
    /** 0x010: Volume descriptor sequence number. */
    uint32_t        uVolumeDescSeqNo;
    /** 0x014: The partition flags (UDF_PARTITION_FLAGS_XXX).   */
    uint16_t        fFlags;
    /** 0x016: The partition number. */
    uint16_t        uPartitionNo;
    /** 0x018: Partition contents (UDF_ENTITY_ID_PD_PARTITION_CONTENTS_XXX). */
    UDFENTITYID     PartitionContents;
    /** 0x038: partition contents use (depends on the PartitionContents field). */
    union
    {
        /** Generic view. */
        uint8_t     ab[128];
        /** UDF partition header descriptor (UDF_ENTITY_ID_PD_PARTITION_CONTENTS_UDF). */
        UDFPARTITIONHDRDESC Hdr;
    } ContentsUse;
    /** 0x0b8: Access type (UDF_PART_ACCESS_TYPE_XXX).  */
    uint32_t        uAccessType;
    /** 0x0bc: Partition starting location (logical sector number). */
    uint32_t        offLocation;
    /** 0x0c0: Partition length in sectors. */
    uint32_t        cSectors;
    /** 0x0c4: Implementation identifier ("*Developer ID"). */
    UDFENTITYID     idImplementation;
    /** 0x0e4: Implementation use bytes. */
    union
    {
        /** Generic view. */
        uint8_t     ab[128];
    } ImplementationUse;
    /** 0x164: Reserved. */
    uint8_t         abReserved[156];
} UDFPARTITIONDESC;
AssertCompileSize(UDFPARTITIONDESC, 512);
/** Pointer to an UDF partitions descriptor. */
typedef UDFPARTITIONDESC *PUDFPARTITIONDESC;
/** Pointer to a const UDF partitions descriptor. */
typedef const UDFPARTITIONDESC *PCUDFPARTITIONDESC;

/** @name UDF_PART_ACCESS_TYPE_XXX - UDF partition access types
 *
 * See @ecma167{3,10.5.7,57}, @udf260{2.2.14.2,51}.
 *
 * @{ */
/** Access not specified by this field. */
#define UDF_PART_ACCESS_TYPE_NOT_SPECIFIED  UINT32_C(0x00000000)
/** Read only: No writes. */
#define UDF_PART_ACCESS_TYPE_READ_ONLY      UINT32_C(0x00000001)
/** Write once: Sectors can only be written once. */
#define UDF_PART_ACCESS_TYPE_WRITE_ONCE     UINT32_C(0x00000002)
/** Rewritable: Logical sectors may require preprocessing before writing. */
#define UDF_PART_ACCESS_TYPE_REWRITABLE     UINT32_C(0x00000003)
/** Overwritable: No restrictions on writing. */
#define UDF_PART_ACCESS_TYPE_OVERWRITABLE   UINT32_C(0x00000004)
/** @} */


/**
 * Logical volume descriptor (LVD) (@ecma167{3,10.6,58}, @udf260{2.2.4,30}).
 *
 * @note Variable length.
 */
typedef struct UDFLOGICALVOLUMEDESC
{
    /** 0x000: The descriptor tag (UDF_TAG_ID_LOGICAL_VOLUME_DESC). */
    UDFTAG          Tag;
    /** 0x010: Volume descriptor sequence number. */
    uint32_t        uVolumeDescSeqNo;
    /** 0x014: Character set used in the achLogicalVolumeID field.   */
    UDFCHARSPEC     DescCharSet;
    /** 0x054: The logical volume ID (label). */
    UDFDSTRING      achLogicalVolumeID[128];
    /** 0x0d4: Logical block size (in bytes). */
    uint32_t        cbLogicalBlock;
    /** 0x0d8: Domain identifier (UDF_ENTITY_ID_LVD_DOMAIN). */
    UDFENTITYID     idDomain;
    /** 0x0f8: Logical volume contents use. */
    union
    {
        /** Byte view. */
        uint8_t     ab[16];
        /** The extent containing the file set descriptor. */
        UDFLONGAD   FileSetDescriptor;
    } ContentsUse;
    /** 0x108: Map table length (in bytes). */
    uint32_t        cbMapTable;
    /** 0x10c: Number of partition maps. */
    uint32_t        cPartitionMaps;
    /** 0x110: Implementation identifier ("*Developer ID"). */
    UDFENTITYID     idImplementation;
    /** 0x130: Implementation use. */
    union
    {
        /** Byte view. */
        uint8_t     ab[128];
    } ImplementationUse;
    /** 0x1b0: Integrity sequence extent. Can be zero if cPartitionMaps is zero. */
    UDFEXTENTAD     IntegritySeqExtent;
    /** 0x1b8: Partition maps (length given by @a cbMapTable), data format is
     * defined by UDFPARTMAPHDR, UDFPARTMAPTYPE1 and UDFPARTMAPTYPE2. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t         abPartitionMaps[RT_FLEXIBLE_ARRAY];
} UDFLOGICALVOLUMEDESC;
AssertCompileMemberOffset(UDFLOGICALVOLUMEDESC, abPartitionMaps, 0x1b8);
/** Pointer to an UDF logical volume descriptor. */
typedef UDFLOGICALVOLUMEDESC *PUDFLOGICALVOLUMEDESC;
/** Pointer to a const UDF logical volume descriptor. */
typedef UDFLOGICALVOLUMEDESC const *PCUDFLOGICALVOLUMEDESC;

/**
 * Partition map header (UDFLOGICALVOLUMEDESC::abPartitionMaps).
 */
typedef struct UDFPARTMAPHDR
{
    /** 0x00: The partition map type. */
    uint8_t         bType;
    /** 0x01: The partition map length (header included). */
    uint8_t         cb;
} UDFPARTMAPHDR;
AssertCompileSize(UDFPARTMAPHDR, 2);
/** Pointer to a partition map header. */
typedef UDFPARTMAPHDR *PUDFPARTMAPHDR;
/** Pointer to a const partition map header. */
typedef UDFPARTMAPHDR const *PCUDFPARTMAPHDR;

/**
 * Partition map type 1 (UDFLOGICALVOLUMEDESC::abPartitionMaps).
 */
typedef struct UDFPARTMAPTYPE1
{
    /** 0x00: Header (uType=1, cb=6). */
    UDFPARTMAPHDR   Hdr;
    /** 0x02: Volume sequence number. */
    uint16_t        uVolumeSeqNo;
    /** 0x04: Partition number. */
    uint16_t        uPartitionNo;
} UDFPARTMAPTYPE1;
AssertCompileSize(UDFPARTMAPTYPE1, 6);
/** Pointer to a type 1 partition map. */
typedef UDFPARTMAPTYPE1 *PUDFPARTMAPTYPE1;
/** Pointer to a const type 1 partition map. */
typedef UDFPARTMAPTYPE1 const *PCUDFPARTMAPTYPE1;

/**
 * Partition map type 2 (UDFLOGICALVOLUMEDESC::abPartitionMaps).
 */
typedef struct UDFPARTMAPTYPE2
{
    /** 0x00: Header (uType=2, cb=64). */
    UDFPARTMAPHDR   Hdr;
    /** 0x02: Reserved \#1. */
    uint16_t        uReserved1;
    /** 0x04: Partition ID type (UDF_ENTITY_ID_VPM_PARTITION_TYPE,
     *  UDF_ENTITY_ID_SPM_PARTITION_TYPE, or UDF_ENTITY_ID_MPM_PARTITION_TYPE). */
    UDFENTITYID     idPartitionType;
    /** 0x24: Volume sequence number. */
    uint16_t        uVolumeSeqNo;
    /** 0x26: Partition number. */
    uint16_t        uPartitionNo;
    /** 0x28: Data specific to the partition ID type. */
    union
    {
        /** 0x28: Generic view. */
        uint8_t     ab[24];

        /** UDF_ENTITY_ID_VPM_PARTITION_TYPE. */
        struct
        {
            /** 0x28: Reserved. */
            uint8_t         abReserved2[24];
        } Vpm;

        /** UDF_ENTITY_ID_SPM_PARTITION_TYPE. */
        struct
        {
            /** 0x28: Packet length in blocks.   */
            uint16_t        cBlocksPerPacket;
            /** 0x2a: Number of sparing tables. */
            uint8_t         cSparingTables;
            /** 0x2b: Reserved padding byte. */
            uint8_t         bReserved2;
            /** 0x2c: The size of each sparing table. */
            uint32_t        cbSparingTable;
            /** 0x30: The sparing table locations (logical block). */
            uint32_t        aoffSparingTables[4];
        } Spm;

        /** UDF_ENTITY_ID_MPM_PARTITION_TYPE. */
        struct
        {
            /** 0x28: Metadata file entry location (logical block). */
            uint32_t        offMetadataFile;
            /** 0x2c: Metadata mirror file entry location (logical block). */
            uint32_t        offMetadataMirrorFile;
            /** 0x30: Metadata bitmap file entry location (logical block). */
            uint32_t        offMetadataBitmapFile;
            /** 0x34: The metadata allocation unit (logical blocks) */
            uint32_t        cBlocksAllocationUnit;
            /** 0x38: The metadata allocation unit alignment (logical blocks). */
            uint16_t        cBlocksAlignmentUnit;
            /** 0x3a: Flags, UDFPARTMAPMETADATA_F_XXX. */
            uint8_t         fFlags;
            /** 0x3b: Reserved. */
            uint8_t         abReserved2[5];
        } Mpm;
    } u;
} UDFPARTMAPTYPE2;
AssertCompileSize(UDFPARTMAPTYPE2, 64);
/** Pointer to a type 2 partition map. */
typedef UDFPARTMAPTYPE2 *PUDFPARTMAPTYPE2;
/** Pointer to a const type 2 partition map. */
typedef UDFPARTMAPTYPE2 const *PCUDFPARTMAPTYPE2;

/** @name UDFPARTMAPMETADATA_F_XXX
 * @{ */
/** Indicates that the metadata is mirrored too, not just the file entry. */
#define UDFPARTMAPMETADATA_F_DATA_MIRRORED      UINT8_C(1)
/** @} */


/**
 * UDF unallocated space descriptor (USD) (@ecma167{3,10.8,61}, @udf260{2.2.5,32}).
 *
 * @note Variable length.
 */
typedef struct UDFUNALLOCATEDSPACEDESC
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_UNALLOCATED_SPACE_DESC). */
    UDFTAG          Tag;
    /** 0x10: Volume descriptor sequence number. */
    uint32_t        uVolumeDescSeqNo;
    /** 0x14: Number of allocation descriptors in the array below. */
    uint32_t        cAllocationDescriptors;
    /** 0x18: Allocation descriptors (variable length). */
    RT_FLEXIBLE_ARRAY_EXTENSION
    UDFEXTENTAD     aAllocationDescriptors[RT_FLEXIBLE_ARRAY];
} UDFUNALLOCATEDSPACEDESC;
AssertCompileMemberOffset(UDFUNALLOCATEDSPACEDESC, aAllocationDescriptors, 0x18);
/** Pointer to an UDF unallocated space descriptor. */
typedef UDFUNALLOCATEDSPACEDESC *PUDFUNALLOCATEDSPACEDESC;
/** Pointer to a const UDF unallocated space descriptor. */
typedef UDFUNALLOCATEDSPACEDESC const *PCUDFUNALLOCATEDSPACEDESC;


/**
 * UDF terminating descriptor (@ecma167{3,10.9,62}, @ecma167{4,14.2,62}).
 */
typedef struct UDFTERMINATINGDESC
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_TERMINATING_DESC). */
    UDFTAG          Tag;
    /** 0x10: Reserved, MBZ. */
    uint8_t         abReserved[496];
} UDFTERMINATINGDESC;
/** Pointer to an UDF terminating descriptor. */
typedef UDFTERMINATINGDESC *PUDFTERMINATINGDESC;
/** Pointer to a const UDF terminating descriptor. */
typedef UDFTERMINATINGDESC const *PCUDFTERMINATINGDESC;


/**
 * UDF logical volume integrity descriptor (LVID) (@ecma167{3,10.10,62},
 * @udf260{2.2.6,32}).
 */
typedef struct UDFLOGICALVOLINTEGRITYDESC
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_LOGICAL_VOLUME_INTEGRITY_DESC). */
    UDFTAG          Tag;
    /** 0x10: Recording timestamp. */
    UDFTIMESTAMP    RecordingTimestamp;
    /** 0x1c: Integrity type (UDF_LVID_TYPE_XXX). */
    uint32_t        uIntegrityType;
    /** 0x20: The next integrity extent. */
    UDFEXTENTAD     NextIntegrityExtent;
    /** 0x28: Number of partitions. */
    uint32_t        cPartitions;
    /** 0x2c: Length of implementation use. */
    uint32_t        cbImplementationUse;
    /**
     * There are two tables each @a cPartitions in size.  The first is the free
     * space table.  The second the size table.
     *
     * Following these tables there are @a cbImplementationUse bytes of space for
     * the implementation to use.
     */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint32_t        aTables[RT_FLEXIBLE_ARRAY];
} UDFLOGICALVOLINTEGRITYDESC;
AssertCompileMemberOffset(UDFLOGICALVOLINTEGRITYDESC, cbImplementationUse, 0x2c);
AssertCompileMemberOffset(UDFLOGICALVOLINTEGRITYDESC, aTables, 0x30);
/** Pointer to an UDF logical volume integrity descriptor.   */
typedef UDFLOGICALVOLINTEGRITYDESC *PUDFLOGICALVOLINTEGRITYDESC;
/** Pointer to a const UDF logical volume integrity descriptor.   */
typedef UDFLOGICALVOLINTEGRITYDESC const *PCUDFLOGICALVOLINTEGRITYDESC;

/** @name UDF_LVID_TYPE_XXX - Integirty types.
 * @{ */
#define UDF_LVID_TYPE_OPEN          UINT32_C(0x00000000)
#define UDF_LVID_TYPE_CLOSE         UINT32_C(0x00000001)
/** @} */

/**
 * UDF file set descriptor (FSD) (@ecma167{4,14.1,86}, @udf260{2.3.2,54}).
 */
typedef struct UDFFILESETDESC
{
    /** 0x000: The descriptor tag (UDF_TAG_ID_FILE_SET_DESC). */
    UDFTAG          Tag;
    /** 0x010: Recording timestamp. */
    UDFTIMESTAMP    RecordingTimestamp;
    /** 0x01c: Interchange level. */
    uint16_t        uInterchangeLevel;
    /** 0x01e: Maximum interchange level. */
    uint16_t        uMaxInterchangeLevel;
    /** 0x020: Character set bitmask (aka list).  Each bit correspond to a
     * character set number. */
    uint32_t        fCharacterSets;
    /** 0x024: Maximum character set bitmask (aka list). */
    uint32_t        fMaxCharacterSets;
    /** 0x028: File set number. */
    uint32_t        uFileSetNo;
    /** 0x02c: File set descriptor number. */
    uint32_t        uFileSetDescNo;
    /** 0x030: Logical volume identifier character set. */
    UDFCHARSPEC     LogicalVolumeIDCharSet;
    /** 0x070: Logical volume identifier string. */
    UDFDSTRING      achLogicalVolumeID[128];
    /** 0x0e0: File set character set. */
    UDFCHARSPEC     FileSetCharSet;
    /** 0x130: Identifier string for this file set. */
    UDFDSTRING      achFileSetID[32];
    /** 0x150: Names a root file containing copyright info.  Optional. */
    UDFDSTRING      achCopyrightFile[32];
    /** 0x170: Names a root file containing an abstract for the file set.  Optional. */
    UDFDSTRING      achAbstractFile[32];
    /** 0x190: Root directory information control block location (ICB).
     * An ICB is a sequence made up of UDF_TAG_ID_FILE_ENTRY,
     * UDF_TAG_ID_INDIRECT_ENTRY, and UDF_TAG_ID_TERMINAL_ENTRY descriptors. */
    UDFLONGAD       RootDirIcb;
    /** 0x1a0: Domain identifier (UDF_ENTITY_FSD_LVD_DOMAIN).  Optional. */
    UDFENTITYID     idDomain;
    /** 0x1c0: Next location with file set descriptors location, 0 if none. */
    UDFLONGAD       NextExtent;
    /** 0x1d0: Location of the system stream directory associated with the
     * file set.  Optional. */
    UDFLONGAD       SystemStreamDirIcb;
    /** 0x1e0: Reserved, MBZ. */
    uint8_t         abReserved[32];
} UDFFILESETDESC;
AssertCompileSize(UDFFILESETDESC, 512);
/** Pointer to an UDF file set descriptor. */
typedef UDFFILESETDESC *PUDFFILESETDESC;
/** Pointer to a const UDF file set descriptor. */
typedef UDFFILESETDESC const *PCUDFFILESETDESC;


/**
 * UDF file identifier descriptor (FID) (@ecma167{4,14.4,91}, @udf260{2.3.4,57}).
 */
typedef struct UDFFILEIDDESC
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_FILE_ID_DESC). */
    UDFTAG          Tag;
    /** 0x10: File version number (1..32767).  Always set to 1. */
    uint16_t        uVersion;
    /** 0x12: File characteristics (UDF_FILE_FLAGS_XXX). */
    uint8_t         fFlags;
    /** 0x13: File identifier (name) length. */
    uint8_t         cbName;
    /** 0x14: Location of an information control block describing the file.
     * Can be null if marked deleted.  The implementation defined part of
     * this contains additional flags and a unique ID. */
    UDFLONGAD       Icb;
    /** 0x24: Length of implementation use field (in bytes).  This can be zero.
     *
     * It can be used to prevent the following FID from spanning a block
     * boundrary, in which case it will be 32 bytes or more, and the it will
     * start with an UDFENTITYID identifying who last wrote it.
     *
     * The latter padding fun is a requirement from write-once media. */
    uint16_t        cbImplementationUse;
    /** 0x26: Two variable sized fields followed by padding to make the
     * actual structure size 4 byte aligned.  The first field in an
     * implementation use field with length given by @a cbImplementationUse.
     * After that is a d-string field with the name of the file, length
     * specified by @a cbName. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t         abImplementationUse[RT_FLEXIBLE_ARRAY];
} UDFFILEIDDESC;
AssertCompileMemberOffset(UDFFILEIDDESC, fFlags,              0x12);
AssertCompileMemberOffset(UDFFILEIDDESC, cbName,              0x13);
AssertCompileMemberOffset(UDFFILEIDDESC, Icb,                 0x14);
AssertCompileMemberOffset(UDFFILEIDDESC, abImplementationUse, 0x26);
/** Pointer to an UDF file set descriptor   */
typedef UDFFILEIDDESC *PUDFFILEIDDESC;
/** Pointer to a const UDF file set descriptor   */
typedef UDFFILEIDDESC const *PCUDFFILEIDDESC;

/** Get the pointer to the name field. */
#define UDFFILEIDDESC_2_NAME(a_pFid)        ((uint8_t const *)(&(a_pFid)->abImplementationUse[(a_pFid)->cbImplementationUse]))
/** Calculates the total size the size of a record.  */
#define UDFFILEIDDESC_CALC_SIZE_EX(cbImplementationUse, cbName) \
    RT_ALIGN_32((uint32_t)RT_UOFFSETOF(UDFFILEIDDESC, abImplementationUse) + cbImplementationUse + cbName, 4)
/** Gets the actual size of a record. */
#define UDFFILEIDDESC_GET_SIZE(a_pFid)      UDFFILEIDDESC_CALC_SIZE_EX((a_pFid)->cbImplementationUse, (a_pFid)->cbName)

/** @name UDF_FILE_FLAGS_XXX
 * @{ */
/** Existence - Hide the file from the user. */
#define UDF_FILE_FLAGS_HIDDEN               UINT8_C(0x01)
/** Directory - Indicates a directory as apposed to some kind of file or symlink or something  (0). */
#define UDF_FILE_FLAGS_DIRECTORY            UINT8_C(0x02)
/** Deleted - Indicate that the file has been deleted.  Assoicated descriptors may still be valid, though. */
#define UDF_FILE_FLAGS_DELETED              UINT8_C(0x04)
/** Parent - Indicate the ICB field refers to the parent directory (or maybe
 * a file in case of streaming directory). */
#define UDF_FILE_FLAGS_PARENT               UINT8_C(0x08)
/** Metadata - Zero means user data, one means implementation specific metadata.
 *  Only allowed used in stream directory. */
#define UDF_FILE_FLAGS_METADATA             UINT8_C(0x10)
/** Reserved bits that should be zer.   */
#define UDF_FILE_FLAGS_RESERVED_MASK        UINT8_C(0xe0)
/** @} */


/**
 * UDF allocation extent descriptor (@ecma167{4,14.5,93}, @udf260{2.3.11,67}).
 */
typedef struct UDFALLOCATIONEXTENTDESC
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_ALLOCATION_EXTENT_DESC). */
    UDFTAG          Tag;
    /** 0x10: Previous allocation extent location (logical block in current
     *  partition). */
    uint32_t        offPrevExtent;
    /** 0x14: Size of the following allocation descriptors (in bytes). */
    uint32_t        cbAllocDescs;
    /** 0x18: Allocation descriptors. */
    union
    {
        UDFSHORTAD  aShortADs[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
        UDFLONGAD   aLongADs[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
        UDFEXTAD    aExtADs[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
    } u;
} UDFALLOCATIONEXTENTDESC;
AssertCompileMemberOffset(UDFALLOCATIONEXTENTDESC, u, 0x18);
/** Pointer to an UDF allocation extent descriptor. */
typedef UDFALLOCATIONEXTENTDESC *PUDFALLOCATIONEXTENTDESC;
/** Pointer to a const UDF allocation extent descriptor. */
typedef UDFALLOCATIONEXTENTDESC const *PCUDFALLOCATIONEXTENTDESC;

/**
 * UDF information control block tag (@ecma167{4,14.6,93}, @udf260{2.3.5,60}).
 */
typedef struct UDFICBTAG
{
    /** 0x00: Number of direct entries in this ICB prior to this one. */
    uint32_t        cEntiresBeforeThis;
    /** 0x04: ICB hierarchy building strategy type (UDF_ICB_STRATEGY_TYPE_XXX). */
    uint16_t        uStrategyType;
    /** 0x06: Type specific parameters. */
    uint8_t         abStrategyParams[2];
    /** 0x08: Max number of direct and indirect entries that MAY be recorded in this ICB. */
    uint16_t        cMaxEntries;
    /** 0x0a: Reserved, MBZ. */
    uint8_t         bReserved;
    /** 0x0b: File type (UDF_FILE_TYPE_XXX). */
    uint8_t         bFileType;
    /** 0x0c: Parent ICB location. */
    UDFLBADDR       ParentIcb;
    /** 0x12: Parent ICB location (UDF_ICB_FLAGS_XXX). */
    uint16_t        fFlags;
} UDFICBTAG;
AssertCompileSize(UDFICBTAG, 20);
typedef UDFICBTAG *PUDFICBTAG;
typedef UDFICBTAG const *PCUDFICBTAG;

/** @name UDF_ICB_STRATEGY_TYPE_XXX - ICB hierarchy building strategies
 *
 * See @ecma167{4,14.6.2,94}, @udf260{6.6,121}
 *
 * @{ */
/** Strategy not specified. */
#define UDF_ICB_STRATEGY_TYPE_NOT_SPECIFIED     UINT16_C(0x0000)
/** See @ecma167{4,A.2,129}. */
#define UDF_ICB_STRATEGY_TYPE_1                 UINT16_C(0x0001)
/** See @ecma167{4,A.3,131}. */
#define UDF_ICB_STRATEGY_TYPE_2                 UINT16_C(0x0002)
/** See @ecma167{4,A.4,131}. */
#define UDF_ICB_STRATEGY_TYPE_3                 UINT16_C(0x0003)
/** See @ecma167{4,A.5,131}. */
#define UDF_ICB_STRATEGY_TYPE_4                 UINT16_C(0x0004)
/** Defined by the UDF spec, see @udf260{6.6,121}. */
#define UDF_ICB_STRATEGY_TYPE_4096              UINT16_C(0x1000)
/** @} */

/** @name UDF_ICB_FLAGS_XXX - ICB flags
 *
 * See @ecma167{4,14.6.8,95}, @udf260{2.3.5.4,61}
 *
 *  @{ */
/** Using UDFSHORTAD. */
#define UDF_ICB_FLAGS_AD_TYPE_SHORT             UINT16_C(0x0000)
/** Using UDFLONGAD. */
#define UDF_ICB_FLAGS_AD_TYPE_LONG              UINT16_C(0x0001)
/** Using UDFEXTAD. */
#define UDF_ICB_FLAGS_AD_TYPE_EXTENDED          UINT16_C(0x0002)
/** File content is embedded in the allocation descriptor area. */
#define UDF_ICB_FLAGS_AD_TYPE_EMBEDDED          UINT16_C(0x0003)
/** Allocation type mask. */
#define UDF_ICB_FLAGS_AD_TYPE_MASK              UINT16_C(0x0007)
/** Set on directories that are sorted (according to @ecma167{4,8.6.1,78}).
 * @note Directories are never sorted in UDF.  */
#define UDF_ICB_FLAGS_SORTED_DIRECTORY          UINT16_C(0x0008)
/** Not relocatable. */
#define UDF_ICB_FLAGS_NON_RELOCATABLE           UINT16_C(0x0010)
/** Indicate that the file needs backing up (DOS attribute). */
#define UDF_ICB_FLAGS_ARCHIVE                   UINT16_C(0x0020)
/** Set UID bit (UNIX). */
#define UDF_ICB_FLAGS_SET_UID                   UINT16_C(0x0040)
/** Set GID bit (UNIX). */
#define UDF_ICB_FLAGS_SET_GID                   UINT16_C(0x0080)
/** Set sticky bit (UNIX). */
#define UDF_ICB_FLAGS_STICKY                    UINT16_C(0x0100)
/** Extents are contiguous. */
#define UDF_ICB_FLAGS_CONTIGUOUS                UINT16_C(0x0200)
/** System bit, reserved for implementation use. */
#define UDF_ICB_FLAGS_SYSTEM                    UINT16_C(0x0400)
/** Data has been transformed in some way.
 * @note UDF shall not set this bit.  */
#define UDF_ICB_FLAGS_TRANSFORMED               UINT16_C(0x0800)
/** Directory may contain multi-versioned files.
 * @note UDF shall not set this bit.  */
#define UDF_ICB_FLAGS_MULTI_VERSIONS            UINT16_C(0x1000)
/** Is a stream in a stream directory. */
#define UDF_ICB_FLAGS_STREAM                    UINT16_C(0x2000)
/** Reserved mask. */
#define UDF_ICB_FLAGS_RESERVED_MASK             UINT16_C(0xc000)
/** @} */

/** @name UDF_FILE_TYPE_XXX - File types
 *
 * See @ecma167{4,14.6.6,94}, @udf260{2.3.5.2,60}
 *
 *  @{ */
#define UDF_FILE_TYPE_NOT_SPECIFIED             UINT8_C(0x00) /**< Not specified by this field. */
#define UDF_FILE_TYPE_UNALLOCATED_SPACE_ENTRY   UINT8_C(0x01)
#define UDF_FILE_TYPE_PARTITION_INTEGRITY_ENTRY UINT8_C(0x02)
#define UDF_FILE_TYPE_INDIRECT_ENTRY            UINT8_C(0x03)
#define UDF_FILE_TYPE_DIRECTORY                 UINT8_C(0x04)
#define UDF_FILE_TYPE_REGULAR_FILE              UINT8_C(0x05)
#define UDF_FILE_TYPE_BLOCK_DEVICE              UINT8_C(0x06)
#define UDF_FILE_TYPE_CHARACTER_DEVICE          UINT8_C(0x07)
#define UDF_FILE_TYPE_EXTENDED_ATTRIBUTES       UINT8_C(0x08)
#define UDF_FILE_TYPE_FIFO                      UINT8_C(0x09)
#define UDF_FILE_TYPE_SOCKET                    UINT8_C(0x0a)
#define UDF_FILE_TYPE_TERMINAL_ENTRY            UINT8_C(0x0b)
#define UDF_FILE_TYPE_SYMBOLIC_LINK             UINT8_C(0x0c)
#define UDF_FILE_TYPE_STREAM_DIRECTORY          UINT8_C(0x0d)
#define UDF_FILE_TYPE_VAT                       UINT8_C(0xf8)
#define UDF_FILE_TYPE_REAL_TIME_FILE            UINT8_C(0xf9)
#define UDF_FILE_TYPE_METADATA_FILE             UINT8_C(0xfa)
#define UDF_FILE_TYPE_METADATA_MIRROR_FILE      UINT8_C(0xfb)
#define UDF_FILE_TYPE_METADATA_BITMAP_FILE      UINT8_C(0xfc)
/** @} */


/**
 * UDF ICB header (derived structure).
 */
typedef struct UDFICBHDR
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_INDIRECT_ENTRY). */
    UDFTAG          Tag;
    /** 0x10: ICB Tag. */
    UDFICBTAG       IcbTag;
} UDFICBHDR;
AssertCompileSize(UDFICBHDR, 36);
/** Pointer to an UDF ICB header. */
typedef UDFICBHDR *PUDFICBHDR;
/** Pointer to a const UDF ICB header. */
typedef UDFICBHDR const *PCUDFICBHDR;


/**
 * UDF indirect entry (@ecma167{4,14.7,96}).
 */
typedef struct UDFINDIRECTENTRY
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_INDIRECT_ENTRY). */
    UDFTAG          Tag;
    /** 0x10: ICB Tag. */
    UDFICBTAG       IcbTag;
    /** 0x24: Indirect ICB location. */
    UDFLONGAD       IndirectIcb;
} UDFINDIRECTENTRY;
AssertCompileSize(UDFINDIRECTENTRY, 52);
/** Pointer to an UDF indirect entry. */
typedef UDFINDIRECTENTRY *PUDFINDIRECTENTRY;
/** Pointer to a const UDF indirect entry. */
typedef UDFINDIRECTENTRY const *PCUDFINDIRECTENTRY;


/**
 * UDF terminal entry (@ecma167{4,14.8,97}).
 */
typedef struct UDFTERMINALENTRY
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_TERMINAL_ENTRY). */
    UDFTAG          Tag;
    /** 0x10: ICB Tag (UDF_FILE_TYPE_TERMINAL_ENTRY). */
    UDFICBTAG       IcbTag;
} UDFTERMINALENTRY;
AssertCompileSize(UDFTERMINALENTRY, 36);
/** Pointer to an UDF terminal entry. */
typedef UDFTERMINALENTRY *PUDFTERMINALENTRY;
/** Pointer to a const UDF terminal entry. */
typedef UDFTERMINALENTRY const *PCUDFTERMINALENTRY;


/**
 * UDF file entry (FE) (@ecma167{4,14.8,97}, @udf260{2.3.6,62}).
 *
 * @note Total length shall not exceed one logical block.
 */
typedef struct UDFFILEENTRY
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_FILE_ENTRY). */
    UDFTAG          Tag;
    /** 0x10: ICB Tag. */
    UDFICBTAG       IcbTag;
    /** 0x24: User ID (UNIX). */
    uint32_t        uid;
    /** 0x28: Group ID (UNIX). */
    uint32_t        gid;
    /** 0x2c: Permission (UDF_PERM_XXX). */
    uint32_t        fPermissions;
    /** 0x30: Number hard links. */
    uint16_t        cHardlinks;
    /** 0x32: Record format (UDF_REC_FMT_XXX).   */
    uint8_t         uRecordFormat;
    /** 0x33: Record format (UDF_REC_ATTR_XXX).   */
    uint8_t         fRecordDisplayAttribs;
    /** 0x34: Record length (in bytes).
     * @note  Must be zero according to the UDF specification. */
    uint32_t        cbRecord;
    /** 0x38: Information length in bytes (file size). */
    uint64_t        cbData;
    /** 0x40: Number of logical blocks allocated (for file data). */
    uint64_t        cLogicalBlocks;
    /** 0x48: Time of last access (prior to recording the file entry). */
    UDFTIMESTAMP    AccessTime;
    /** 0x54: Time of last data modification. */
    UDFTIMESTAMP    ModificationTime;
    /** 0x60: Time of last attribute/status modification. */
    UDFTIMESTAMP    ChangeTime;
    /** 0x6c: Checkpoint number (defaults to 1). */
    uint32_t        uCheckpoint;
    /** 0x70: Extended attribute information control block location. */
    UDFLONGAD       ExtAttribIcb;
    /** 0x80: Implementation identifier ("*Developer ID"). */
    UDFENTITYID     idImplementation;
    /** 0xa0: Unique ID. */
    uint64_t        INodeId;
    /** 0xa8: Length of extended attributes in bytes, multiple of four. */
    uint32_t        cbExtAttribs;
    /** 0xac: Length of allocation descriptors in bytes, multiple of four. */
    uint32_t        cbAllocDescs;
    /** 0xb0: Two variable sized fields.  First @a cbExtAttribs bytes of extended
     *  attributes, then @a cbAllocDescs bytes of allocation descriptors. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t         abExtAttribs[RT_FLEXIBLE_ARRAY];
} UDFFILEENTRY;
AssertCompileMemberOffset(UDFFILEENTRY, abExtAttribs, 0xb0);
/** Pointer to an UDF file entry. */
typedef UDFFILEENTRY *PUDFFILEENTRY;
/** Pointer to a const UDF file entry. */
typedef UDFFILEENTRY const *PCUDFFILEENTRY;

/** @name UDF_PERM_XXX - UDFFILEENTRY::fPermissions
 * See @ecma167{4,14.9.5,99}.
 * @{ */
#define UDF_PERM_OTH_EXEC          UINT32_C(0x00000001)
#define UDF_PERM_OTH_WRITE         UINT32_C(0x00000002)
#define UDF_PERM_OTH_READ          UINT32_C(0x00000004)
#define UDF_PERM_OTH_ATTRIB        UINT32_C(0x00000008)
#define UDF_PERM_OTH_DELETE        UINT32_C(0x00000010)
#define UDF_PERM_OTH_MASK          UINT32_C(0x0000001f)

#define UDF_PERM_GRP_EXEC          UINT32_C(0x00000020)
#define UDF_PERM_GRP_WRITE         UINT32_C(0x00000040)
#define UDF_PERM_GRP_READ          UINT32_C(0x00000080)
#define UDF_PERM_GRP_ATTRIB        UINT32_C(0x00000100)
#define UDF_PERM_GRP_DELETE        UINT32_C(0x00000200)
#define UDF_PERM_GRP_MASK          UINT32_C(0x000003e0)

#define UDF_PERM_USR_EXEC          UINT32_C(0x00000400)
#define UDF_PERM_USR_WRITE         UINT32_C(0x00000800)
#define UDF_PERM_USR_READ          UINT32_C(0x00001000)
#define UDF_PERM_USR_ATTRIB        UINT32_C(0x00002000)
#define UDF_PERM_USR_DELETE        UINT32_C(0x00004000)
#define UDF_PERM_USR_MASK          UINT32_C(0x00007c00)

#define UDF_PERM_USR_RESERVED_MASK UINT32_C(0xffff8000)
/** @} */

/** @name UDF_REC_FMT_XXX - Record format.
 * See @ecma167{4,14.9.7,100}.
 * @{ */
/** Not record format specified.
 * @note The only allowed value according to the UDF specification. */
#define UDF_REC_FMT_NOT_SPECIFIED       UINT8_C(0x00)
/** @} */

/** @name UDF_REC_ATTR_XXX - Record display attributes.
 * See @ecma167{4,14.9.8,100}.
 * @{ */
/** Manner of record display not specified.
 * @note The only allowed value according to the UDF specification. */
#define UDF_REC_ATTR_NOT_SPECIFIED      UINT8_C(0x00)
/** @} */


/**
 * UDF extended attribute header descriptor (@ecma167{4,14.10.1,102},
 * @udf260{3.3.4,79}).
 */
typedef struct UDFEXTATTRIBHDRDESC
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_EXTENDED_ATTRIB_HDR_DESC). */
    UDFTAG          Tag;
    /** 0x10: Implementation attributes location (byte offset) into the EA space.
     * This typically set to UINT32_MAX if not present, though any value larger
     * than the EA space will do. */
    uint32_t        offImplementationAttribs;
    /** 0x14: Application attributes location (byte offset) into the EA space.
     * This typically set to UINT32_MAX if not present, though any value larger
     * than the EA space will do. */
    uint32_t        offApplicationAttribs;
} UDFEXTATTRIBHDRDESC;
AssertCompileSize(UDFEXTATTRIBHDRDESC, 24);
/** Pointer to an UDF extended attribute header descriptor.  */
typedef UDFEXTATTRIBHDRDESC *PUDFEXTATTRIBHDRDESC;
/** Pointer to a const UDF extended attribute header descriptor.  */
typedef UDFEXTATTRIBHDRDESC const *PCUDFEXTATTRIBHDRDESC;

/**
 * UDF character set info EA data (@ecma167{4,14.10.3,104}).
 *
 * Not needed by UDF.
 */
typedef struct UDFEADATACHARSETINFO
{
    /** 0x00/0x0c: The length of the escape sequences (in bytes). */
    uint32_t        cbEscSeqs;
    /** 0x04/0x10: The character set type (UDF_CHAR_SET_TYPE_XXX). */
    uint8_t         bType;
    /** 0x05/0x11: Escape sequences. */
    uint8_t         abEscSeqs[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} UDFEADATACHARSETINFO;
/** Pointer to UDF character set info EA data. */
typedef UDFEADATACHARSETINFO *PUDFEADATACHARSETINFO;
/** Pointer to const UDF character set info EA data. */
typedef UDFEADATACHARSETINFO const *PCUDFEADATACHARSETINFO;
/** UDFGEA::uAttribType value for UDFEADATACHARSETINFO.*/
#define UDFEADATACHARSETINFO_ATTRIB_TYPE        UINT32_C(0x00000001)
/** UDFGEA::uAttribSubtype value for UDFEADATACHARSETINFO.   */
#define UDFEADATACHARSETINFO_ATTRIB_SUBTYPE     UINT32_C(0x00000001)

/**
 * UDF alternate permissions EA data (@ecma167{4,14.10.4,105}, @udf260{3.3.4.2,80}).
 * @note Not recorded according to the UDF specification.
 */
typedef struct UDFEADATAALTPERM
{
    /** 0x00/0x0c: Alternative owner ID. */
    uint16_t        idOwner;
    /** 0x02/0x0e: Alternative group ID. */
    uint16_t        idGroup;
    /** 0x04/0x10: Alternative permissions.   */
    uint16_t        fPermission;
} UDFEADATAALTPERM;
/** Pointer to UDF alternative permissions EA data. */
typedef UDFEADATAALTPERM *PUDFEADATAALTPERM;
/** Pointer to const UDF alternative permissions EA data. */
typedef UDFEADATAALTPERM const *PCUDFEADATAALTPERM;
/** UDFGEA::uAttribType value for UDFEADATAALTPERM.   */
#define UDFEADATAALTPERM_ATTRIB_TYPE            UINT32_C(0x00000003)
/** UDFGEA::uAttribSubtype value for UDFEADATAALTPERM.   */
#define UDFEADATAALTPERM_ATTRIB_SUBTYPE         UINT32_C(0x00000001)

/**
 * UDF file times EA data (@ecma167{4,14.10.5,108}, @udf260{3.3.4.3,80}).
 * (This is a bit reminiscent of ISO9660RRIPTF.)
 */
typedef struct UDFEADATAFILETIMES
{
    /** 0x00/0x0c: Timestamp length. */
    uint32_t        cbTimestamps;
    /** 0x04/0x10: Indicates which timestamps are present
     * (UDF_FILE_TIMES_EA_F_XXX). */
    uint32_t        fFlags;
    /** 0x08/0x14: Timestamps. */
    UDFTIMESTAMP    aTimestamps[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} UDFEADATAFILETIMES;
/** Pointer to UDF file times EA data. */
typedef UDFEADATAFILETIMES *PUDFEADATAFILETIMES;
/** Pointer to const UDF file times EA data. */
typedef UDFEADATAFILETIMES const *PCUDFEADATAFILETIMES;
/** UDFGEA::uAttribType value for UDFEADATAFILETIMES.   */
#define UDFEADATAFILETIMES_ATTRIB_TYPE          UINT32_C(0x00000005)
/** UDFGEA::uAttribSubtype value for UDFEADATAFILETIMES.   */
#define UDFEADATAFILETIMES_ATTRIB_SUBTYPE       UINT32_C(0x00000001)

/** @name UDF_FILE_TIMES_EA_F_XXX - File times existence flags.
 * See @ecma167{4,14.10.5.6,109}
 * @{ */
#define UDF_FILE_TIMES_EA_F_BIRTH           UINT8_C(0x01) /**< Birth (creation) timestamp is recorded. */
#define UDF_FILE_TIMES_EA_F_DELETE          UINT8_C(0x04) /**< Deletion timestamp is recorded. */
#define UDF_FILE_TIMES_EA_F_EFFECTIVE       UINT8_C(0x08) /**< Effective timestamp is recorded. */
#define UDF_FILE_TIMES_EA_F_BACKUP          UINT8_C(0x20) /**< Backup timestamp is recorded. */
#define UDF_FILE_TIMES_EA_F_RESERVED_MASK   UINT8_C(0xd2)
/** @} */

/**
 * UDF information times EA data (@ecma167{4,14.10.6,109}).
 */
typedef struct UDFEADATAINFOTIMES
{
    /** 0x00/0x0c: Timestamp length. */
    uint32_t        cbTimestamps;
    /** 0x04/0x10: Indicates which timestamps are present
     * (UDF_INFO_TIMES_EA_F_XXX). */
    uint32_t        fFlags;
    /** 0x08/0x14: Timestamps. */
    UDFTIMESTAMP    aTimestamps[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} UDFEADATAINFOTIMES;
/** Pointer to UDF information times EA data. */
typedef UDFEADATAINFOTIMES *PUDFEADATAINFOTIMES;
/** Pointer to const UDF information times EA data. */
typedef UDFEADATAINFOTIMES const *PCUDFEADATAINFOTIMES;
/** UDFGEA::uAttribType value for UDFEADATAINFOTIMES.   */
#define UDFEADATAINFOTIMES_ATTRIB_TYPE          UINT32_C(0x00000006)
/** UDFGEA::uAttribSubtype value for UDFEADATAINFOTIMES.   */
#define UDFEADATAINFOTIMES_ATTRIB_SUBTYPE       UINT32_C(0x00000001)

/** @name UDF_INFO_TIMES_EA_F_XXX - Information times existence flags.
 * See @ecma167{4,14.10.6.6,110}
 * @{ */
#define UDF_INFO_TIMES_EA_F_BIRTH           UINT8_C(0x01) /**< Birth (creation) timestamp is recorded. */
#define UDF_INFO_TIMES_EA_F_MODIFIED        UINT8_C(0x02) /**< Last (data) modified timestamp is recorded. */
#define UDF_INFO_TIMES_EA_F_EXPIRE          UINT8_C(0x04) /**< Expiration (deletion) timestamp is recorded. */
#define UDF_INFO_TIMES_EA_F_EFFECTIVE       UINT8_C(0x08) /**< Effective timestamp is recorded. */
#define UDF_INFO_TIMES_EA_F_RESERVED_MASK   UINT8_C(0xf0)
/** @} */

/**
 * UDF device specification EA data (@ecma167{4,14.10.7,110}, @udf260{3.3.4.4,81}).
 */
typedef struct UDFEADATADEVICESPEC
{
    /** 0x00/0x0c: Length of implementation use field. */
    uint32_t        cbImplementationUse;
    /** 0x04/0x10: Major device number. */
    uint32_t        uMajorDeviceNo;
    /** 0x08/0x14: Minor device number. */
    uint32_t        uMinorDeviceNo;
    /** 0x0c/0x18: Implementation use field (variable length).
     * UDF specficiation expects UDFENTITYID with a "*Developer ID" as first part
     * here. */
    uint8_t         abImplementationUse[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} UDFEADATADEVICESPEC;
/** Pointer to UDF device specification EA data. */
typedef UDFEADATADEVICESPEC *PUDFEADATADEVICESPEC;
/** Pointer to const UDF device specification EA data. */
typedef UDFEADATADEVICESPEC const *PCUDFEADATADEVICESPEC;
/** UDFGEA::uAttribType value for UDFEADATADEVICESPEC.   */
#define UDFEADATADEVICESPEC_ATTRIB_TYPE         UINT32_C(0x0000000c)
/** UDFGEA::uAttribSubtype value for UDFEADATADEVICESPEC.   */
#define UDFEADATADEVICESPEC_ATTRIB_SUBTYPE      UINT32_C(0x00000001)

/**
 * UDF free EA space payload for implementation and application use EAs
 * (@udf260{3.3.4.5.1.1,82}, @udf260{3.3.4.6.1.1,88}).
 *
 * UDFEADATAIMPLUSE::idImplementation is UDF_ENTITY_ID_IUEA_FREE_EA_SPACE.
 * UDFEADATAAPPUSE::idImplementation is UDF_ENTITY_ID_AUEA_FREE_EA_SPACE.
 */
typedef struct UDFFREEEASPACE
{
    /** 0x00/0x30: Header checksum.
     * @note 16-bit checksum of UDFGEA up thru u.ImplUse.idImplementation. */
    uint16_t        uChecksum;
    /** 0x02/0x32: Free space. */
    uint8_t         abFree[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} UDFFREEEASPACE;
/** Pointer to UDF free EA space impl/app use payload. */
typedef UDFFREEEASPACE *PUDFFREEEASPACE;
/** Pointer to const UDF free EA space impl/app use payload. */
typedef UDFFREEEASPACE const *PCUDFFREEEASPACE;

/**
 * UDF DVD copyright management information implementation use EA payload
 * (@udf260{3.3.4.5.1.2,83}).
 *
 * UDFEADATAIMPLUSE::idImplementation is UDF_ENTITY_ID_IUEA_DVD_CGMS_INFO.
 */
typedef struct UDFIUEADVDCGMSINFO
{
    /** 0x00/0x30: Header checksum.
     * @note 16-bit checksum of UDFGEA up thru u.ImplUse.idImplementation. */
    uint16_t        uChecksum;
    /** 0x02/0x32: The CGMS information (whatever that is). */
    uint8_t         bInfo;
    /** 0x03/0x33: Data structure type (whatever that is). */
    uint8_t         bType;
    /** 0x04/0x34: Production system information, probably dependend on the
     * values of previous fields. */
    uint8_t         abProtSysInfo[4];
} UDFIUEADVDCGMSINFO;
/** Pointer to UDF DVD copyright management information implementation use EA payload. */
typedef UDFIUEADVDCGMSINFO *PUDFIUEADVDCGMSINFO;
/** Pointer to const UDF DVD copyright management information implementation use EA payload. */
typedef UDFIUEADVDCGMSINFO const *PCUDFIUEADVDCGMSINFO;

/**
 * UDF OS/2 EA length implementation use EA payload (@udf260{3.3.4.5.3.1,84}).
 *
 * UDFEADATAIMPLUSE::idImplementation is UDF_ENTITY_ID_IUEA_OS2_EA_LENGTH.
 */
#pragma pack(2)
typedef struct UDFIUEAOS2EALENGTH
{
    /** 0x00/0x30: Header checksum.
     * @note 16-bit checksum of UDFGEA up thru u.ImplUse.idImplementation. */
    uint16_t        uChecksum;
    /** 0x02/0x32: The CGMS information (whatever that is). */
    uint32_t        cbEAs;
} UDFIUEAOS2EALENGTH;
#pragma pack()
AssertCompileMemberOffset(UDFIUEAOS2EALENGTH, cbEAs, 2);
/** Pointer to UDF OS/2 EA length implementation use EA payload. */
typedef UDFIUEAOS2EALENGTH *PUDFIUEAOS2EALENGTH;
/** Pointer to const UDF OS/2 EA length implementation use EA payload. */
typedef UDFIUEAOS2EALENGTH const *PCUDFIUEAOS2EALENGTH;

/**
 * UDF Mac volume info implementation use EA payload (@udf260{3.3.4.5.4.1,84}).
 *
 * UDFEADATAIMPLUSE::idImplementation is UDF_ENTITY_ID_IUEA_MAC_VOLUME_INFO.
 */
#pragma pack(2)
typedef struct UDFIUEAMACVOLINFO
{
    /** 0x00/0x30: Header checksum.
     * @note 16-bit checksum of UDFGEA up thru u.ImplUse.idImplementation. */
    uint16_t        uChecksum;
    /** 0x02/0x32: Last modification time. */
    UDFTIMESTAMP    LastModificationTime;
    /** 0x0e/0x3e: Last backup time. */
    UDFTIMESTAMP    LastBackupTime;
    /** 0x1a/0x4e: Volume finder information. */
    uint32_t        au32FinderInfo[8];
} UDFIUEAMACVOLINFO;
#pragma pack()
AssertCompileMemberOffset(UDFIUEAMACVOLINFO, au32FinderInfo, 0x1a);
/** Pointer to UDF Mac volume info implementation use EA payload. */
typedef UDFIUEAMACVOLINFO *PUDFIUEAMACVOLINFO;
/** Pointer to const UDF Mac volume info implementation use EA payload. */
typedef UDFIUEAMACVOLINFO const *PCUDFIUEAMACVOLINFO;

/**
 * UDF point for use in Mac EAs (@udf260{3.3.4.5.4.2,86}).
 */
typedef struct UDFMACPOINT
{
    /** X coordinate. */
    int16_t         x;
    /** Y coordinate. */
    int16_t         y;
} UDFMACPOINT;

/**
 * UDF rectangle for using Mac EAs (@udf260{3.3.4.5.4.2,86}).
 */
typedef struct UDFMACRECT
{
    /** top Y coordinate. */
    int16_t         yTop;
    /** left X coordinate. */
    int16_t         xLeft;
    /** bottom Y coordinate. (exclusive?) */
    int16_t         yBottom;
    /** right X coordinate. (exclusive?) */
    int16_t         xRight;
} UDFMACRECT;

/**
 * UDF finder directory info for Mac EAs (@udf260{3.3.4.5.4.2,86}).
 */
typedef struct UDFMACFDINFO
{
    UDFMACRECT      FrRect;
    int16_t         FrFlags;
    UDFMACPOINT     FrLocation;
    int16_t         FrView;
} UDFMACFDINFO;
AssertCompileSize(UDFMACFDINFO, 16);

/**
 * UDF finder directory extended info for Mac EAs (@udf260{3.3.4.5.4.2,86}).
 */
typedef struct UDFMACFDXINFO
{
    UDFMACPOINT     FrScroll;
    int32_t         FrOpenChain;
    uint8_t         FrScript;
    uint8_t         FrXFlags;
    uint16_t        FrComment;
    uint32_t        FrPutAway;
} UDFMACFDXINFO;
AssertCompileSize(UDFMACFDXINFO, 16);

/**
 * UDF Mac finder info implementation use EA payload (@udf260{3.3.4.5.4.1,84}),
 * directory edition.
 *
 * UDFEADATAIMPLUSE::idImplementation is UDF_ENTITY_ID_IUEA_MAC_FINDER_INFO.
 */
typedef struct UDFIUEAMACFINDERINFODIR
{
    /** 0x00/0x30: Header checksum.
     * @note 16-bit checksum of UDFGEA up thru u.ImplUse.idImplementation. */
    uint16_t        uChecksum;
    /** 0x02/0x32: Explicit alignment padding, MBZ. */
    uint16_t        uPadding;
    /** 0x04/0x34: Parent directory ID. */
    uint32_t        idParentDir;
    /** 0x08/0x38: Dir information. */
    UDFMACFDINFO    DirInfo;
    /** 0x18/0x48: Dir extended information. */
    UDFMACFDXINFO   DirExInfo;
} UDFIUEAMACFINDERINFODIR;
AssertCompileMemberOffset(UDFIUEAMACFINDERINFODIR, DirInfo, 0x08);
AssertCompileMemberOffset(UDFIUEAMACFINDERINFODIR, DirExInfo, 0x18);
AssertCompileSize(UDFIUEAMACFINDERINFODIR, 0x28);
/** Pointer to UDF Mac finder info for dir implementation use EA payload. */
typedef UDFIUEAMACFINDERINFODIR *PUDFIUEAMACFINDERINFODIR;
/** Pointer to const UDF Mac finder info for dir implementation use EA payload. */
typedef UDFIUEAMACFINDERINFODIR const *PCUDFIUEAMACFINDERINFODIR;

/**
 * UDF finder file info for Mac EAs (@udf260{3.3.4.5.4.2,86}).
 */
typedef struct UDFMACFFINFO
{
    uint32_t        FrType;
    uint32_t        FrCreator;
    uint16_t        FrFlags;
    UDFMACPOINT     FrLocation;
    int16_t         FrFldr;
} UDFMACFFINFO;
AssertCompileSize(UDFMACFFINFO, 16);

/**
 * UDF finder file extended info for Mac EAs (@udf260{3.3.4.5.4.2,86}).
 */
typedef struct UDFMACFFXINFO
{
    int16_t         FrIconID;
    uint8_t         FdUnused[6];
    uint8_t         FrScript;
    uint8_t         FrXFlags;
    uint16_t        FrComment;
    uint32_t        FrPutAway;
} UDFMACFFXINFO;
AssertCompileSize(UDFMACFFXINFO, 16);

/**
 * UDF Mac finder info implementation use EA payload (@udf260{3.3.4.5.4.1,84}),
 * file edition.
 *
 * UDFEADATAIMPLUSE::idImplementation is UDF_ENTITY_ID_IUEA_MAC_FINDER_INFO.
 */
typedef struct UDFIUEAMACFINDERINFOFILE
{
    /** 0x00/0x30: Header checksum.
     * @note 16-bit checksum of UDFGEA up thru u.ImplUse.idImplementation. */
    uint16_t        uChecksum;
    /** 0x02/0x32: Explicit alignment padding, MBZ. */
    uint16_t        uPadding;
    /** 0x04/0x34: Parent directory ID. */
    uint32_t        idParentDir;
    /** 0x08/0x38: File information. */
    UDFMACFFINFO    FileInfo;
    /** 0x18/0x48: File extended information. */
    UDFMACFFXINFO   FileExInfo;
    /** 0x28/0x58: The size of the fork data (in bytes). */
    uint32_t        cbForkData;
    /** 0x2c/0x5c: The size of the fork allocation (in bytes). */
    uint32_t        cbForkAlloc;
} UDFIUEAMACFINDERINFOFILE;
AssertCompileMemberOffset(UDFIUEAMACFINDERINFOFILE, FileInfo, 0x08);
AssertCompileMemberOffset(UDFIUEAMACFINDERINFOFILE, FileExInfo, 0x18);
AssertCompileMemberOffset(UDFIUEAMACFINDERINFOFILE, cbForkData, 0x28);
AssertCompileSize(UDFIUEAMACFINDERINFOFILE, 0x30);
/** Pointer to UDF Mac finder info for file implementation use EA payload. */
typedef UDFIUEAMACFINDERINFOFILE *PUDFIUEAMACFINDERINFOFILE;
/** Pointer to const UDF Mac finder info for file implementation use EA payload. */
typedef UDFIUEAMACFINDERINFOFILE const *PCUDFIUEAMACFINDERINFOFILE;

/**
 * UDF OS/400 directory info implementation use EA payload (@udf260{3.3.4.5.6.1,87})
 *
 * UDFEADATAIMPLUSE::idImplementation is UDF_ENTITY_ID_IUEA_OS400_DIR_INFO.
 */
typedef struct UDFIUEAOS400DIRINFO
{
    /** 0x00/0x30: Header checksum.
     * @note 16-bit checksum of UDFGEA up thru u.ImplUse.idImplementation. */
    uint16_t        uChecksum;
    /** 0x02/0x32: Explicit alignment padding, MBZ. */
    uint16_t        uPadding;
    /** 0x04/0x34: The directory info, format documented elsewhere. */
    uint8_t         abDirInfo[44];
} UDFIUEAOS400DIRINFO;
AssertCompileSize(UDFIUEAOS400DIRINFO, 0x30);
/** Pointer to UDF Mac finder info for file implementation use EA payload. */
typedef UDFIUEAOS400DIRINFO *PUDFIUEAOS400DIRINFO;
/** Pointer to const UDF Mac finder info for file implementation use EA payload. */
typedef UDFIUEAOS400DIRINFO const *PCUDFIUEAOS400DIRINFO;


/**
 * UDF implementation use EA data (@ecma167{4,14.10.8,111}, @udf260{3.3.4.5,82}).
 */
typedef struct UDFEADATAIMPLUSE
{
    /** 0x00/0x0c: Length uData in bytes. */
    uint32_t        cbData;
    /** 0x04/0x10: Implementation identifier (UDF_ENTITY_ID_IUEA_XXX). */
    UDFENTITYID     idImplementation;
    /** 0x24/0x30: Implementation use field (variable length). */
    union
    {
        /** Generic byte view. */
        uint8_t                     abData[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
        /** Free EA space (UDF_ENTITY_ID_IUEA_FREE_EA_SPACE). */
        UDFFREEEASPACE              FreeEaSpace;
        /** DVD copyright management information (UDF_ENTITY_ID_IUEA_DVD_CGMS_INFO). */
        UDFIUEADVDCGMSINFO          DvdCgmsInfo;
        /** OS/2 EA length (UDF_ENTITY_ID_IUEA_OS2_EA_LENGTH). */
        UDFIUEAOS2EALENGTH          Os2EaLength;
        /** Mac volume info (UDF_ENTITY_ID_IUEA_MAC_VOLUME_INFO). */
        UDFIUEAMACVOLINFO           MacVolInfo;
        /** Mac finder info, directory edition (UDF_ENTITY_ID_IUEA_MAC_FINDER_INFO). */
        UDFIUEAMACFINDERINFODIR     MacFinderInfoDir;
        /** Mac finder info, file edition (UDF_ENTITY_ID_IUEA_MAC_FINDER_INFO). */
        UDFIUEAMACFINDERINFOFILE    MacFinderInfoFile;
        /** OS/400 directory info (UDF_ENTITY_ID_IUEA_OS400_DIR_INFO). */
        UDFIUEAOS400DIRINFO         Os400DirInfo;
    } u;
} UDFEADATAIMPLUSE;
/** Pointer to UDF implementation use EA data. */
typedef UDFEADATAIMPLUSE *PUDFEADATAIMPLUSE;
/** Pointer to const UDF implementation use EA data. */
typedef UDFEADATAIMPLUSE const *PCUDFEADATAIMPLUSE;
/** UDFGEA::uAttribType value for UDFEADATAIMPLUSE.   */
#define UDFEADATAIMPLUSE_ATTRIB_TYPE            UINT32_C(0x00000800)
/** UDFGEA::uAttribSubtype value for UDFEADATAIMPLUSE.   */
#define UDFEADATAIMPLUSE_ATTRIB_SUBTYPE         UINT32_C(0x00000001)

/**
 * UDF application use EA data (@ecma167{4,14.10.9,112}, @udf260{3.3.4.6,88}).
 */
typedef struct UDFEADATAAPPUSE
{
    /** 0x0c: Length uData in bytes. */
    uint32_t        cbData;
    /** 0x10: Application identifier (UDF_ENTITY_ID_AUEA_FREE_EA_SPACE). */
    UDFENTITYID     idApplication;
    /** 0x30: Application use field (variable length). */
    union
    {
        /** Generic byte view. */
        uint8_t             ab[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
        /** Free EA space (UDF_ENTITY_ID_AUEA_FREE_EA_SPACE). */
        UDFFREEEASPACE      FreeEaSpace;
    } uData;
} UDFEADATAAPPUSE;
/** Pointer to UDF application use EA data. */
typedef UDFEADATAAPPUSE *PUDFEADATAAPPUSE;
/** Pointer to const UDF application use EA data. */
typedef UDFEADATAAPPUSE const *PCUDFEADATAAPPUSE;
/** UDFGEA::uAttribType value for UDFEADATAAPPUSE.   */
#define UDFEADATAAPPUSE_ATTRIB_TYPE             UINT32_C(0x00010000)
/** UDFGEA::uAttribSubtype value for UDFEADATAAPPUSE.   */
#define UDFEADATAAPPUSE_ATTRIB_SUBTYPE          UINT32_C(0x00000001)

/**
 * UDF generic extended attribute (@ecma167{4,14.10.2,103}).
 */
typedef struct UDFGEA
{
    /** 0x00: Attribute type (UDFXXX_ATTRIB_TYPE). */
    uint32_t        uAttribType;
    /** 0x04: Attribute subtype (UDFXXX_ATTRIB_SUBTYPE). */
    uint8_t         uAttribSubtype;
    /** 0x05: Reserved padding bytes, MBZ. */
    uint8_t         abReserved[3];
    /** 0x08: Size of the whole extended attribute.
     * Multiple of four is recommended. */
    uint32_t        cbAttrib;
    /** 0x0c: Attribute data union. */
    union
    {
        /** Generic byte view (variable size). */
        uint8_t                 abData[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
        /** Character set information (@ecma167{4,14.10.3,104}). */
        UDFEADATACHARSETINFO    CharSetInfo;
        /** Alternate permissions (@ecma167{4,14.10.4,105}, @udf260{3.3.4.2,80}).
         * @note Not recorded according to the UDF specification.  */
        UDFEADATAALTPERM        AltPerm;
        /** File times (@ecma167{4,14.10.5,108}, @udf260{3.3.4.3,80}).
         * (This is a bit reminiscent of ISO9660RRIPTF.) */
        UDFEADATAFILETIMES      FileTimes;
        /** Information times (@ecma167{4,14.10.6,109}). */
        UDFEADATAINFOTIMES      InfoTimes;
        /** Device specification (@ecma167{4,14.10.7,110}, @udf260{3.3.4.4,81}). */
        UDFEADATADEVICESPEC     DeviceSpec;
        /** Implementation use (@ecma167{4,14.10.8,111}, @udf260{3.3.4.5,82}). */
        UDFEADATAIMPLUSE        ImplUse;
        /** Application use (@ecma167{4,14.10.9,112}, @udf260{3.3.4.6,88}). */
        UDFEADATAAPPUSE         AppUse;
    } u;
} UDFGEA;
AssertCompileMemberOffset(UDFGEA, u, 0x0c);
/** Pointer to a UDF extended attribute. */
typedef UDFGEA *PUDFGEA;
/** Pointer to a const UDF extended attribute. */
typedef UDFGEA const *PCUDFGEA;


/**
 * UDF unallocated space entry (@ecma167{4,14.11,113}, @udf260{2.3.7,64}).
 *
 * @note Total length shall not exceed one logical block.
 */
typedef struct UDFUNALLOCATEDSPACEENTRY
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_UNALLOCATED_SPACE_ENTRY). */
    UDFTAG          Tag;
    /** 0x10: ICB Tag. */
    UDFICBTAG       IcbTag;
    /** 0x24: Size of the allocation desciptors in bytes. */
    uint32_t        cbAllocDescs;
    /** 0x28: Allocation desciptors, type given by IcbTag::fFlags. */
    union
    {
        UDFSHORTAD  aShortADs[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
        UDFLONGAD   aLongADs[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
        UDFEXTAD    aExtADs[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
        UDFEXTENTAD SingleAD;
    } u;
} UDFUNALLOCATEDSPACEENTRY;
AssertCompileMemberOffset(UDFUNALLOCATEDSPACEENTRY, u, 0x28);
/** Pointer to an UDF unallocated space entry. */
typedef UDFUNALLOCATEDSPACEENTRY *PUDFUNALLOCATEDSPACEENTRY;
/** Pointer to a const UDF unallocated space entry. */
typedef UDFUNALLOCATEDSPACEENTRY const *PCUDFUNALLOCATEDSPACEENTRY;


/**
 * UDF space bitmap descriptor (SBD) (@ecma167{4,14.12,114}, @udf260{2.3.8,65}).
 */
typedef struct UDFSPACEBITMAPDESC
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_SPACE_BITMAP_DESC). */
    UDFTAG          Tag;
    /** 0x10: Number of bits in the bitmap. */
    uint32_t        cBits;
    /** 0x14: The bitmap size in bytes. */
    uint32_t        cbBitmap;
    /** 0x18: The bitmap. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t         abBitmap[RT_FLEXIBLE_ARRAY];
} UDFSPACEBITMAPDESC;
AssertCompileMemberOffset(UDFSPACEBITMAPDESC, abBitmap, 0x18);
/** Pointer to an UDF space bitmap descriptor. */
typedef UDFSPACEBITMAPDESC *PUDFSPACEBITMAPDESC;
/** Pointer to a const UDF space bitmap descriptor. */
typedef UDFSPACEBITMAPDESC const *PCUDFSPACEBITMAPDESC;


/**
 * UDF partition integrity descriptor (@ecma167{4,14.3,115}, @udf260{2.3.9,65}).
 *
 * @note Not needed by UDF.
 */
typedef struct UDFPARTITIONINTEGRITYDESC
{
    /** 0x000: The descriptor tag (UDF_TAG_ID_PARTITION_INTEGERITY_DESC). */
    UDFTAG          Tag;
    /** 0x010: ICB Tag. */
    UDFICBTAG       IcbTag;
    /** 0x024: Recording timestamp. */
    UDFTIMESTAMP    RecordingTimestamp;
    /** 0x030: Interity type (UDF_PARTITION_INTEGRITY_TYPE_XXX). */
    uint8_t         bType;
    /** 0x031: Reserved. */
    uint8_t         abReserved[175];
    /** 0x0e0: Implementation identifier. */
    UDFENTITYID     idImplementation;
    /** 0x100: Implementation use data. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t         abImplementationUse[RT_FLEXIBLE_ARRAY];
} UDFPARTITIONINTEGRITYDESC;
AssertCompileMemberOffset(UDFPARTITIONINTEGRITYDESC, abImplementationUse, 0x100);
/** Pointer to an UDF partition integrity descriptor. */
typedef UDFPARTITIONINTEGRITYDESC *PUDFPARTITIONINTEGRITYDESC;
/** Pointer to a const UDF partition integrity descriptor. */
typedef UDFPARTITIONINTEGRITYDESC const *PCUDFPARTITIONINTEGRITYDESC;


/**
 * UDF extended file entry (EFE) (@ecma167{4,14.17,120}, @udf260{3.3.5,83}).
 *
 * @note Total length shall not exceed one logical block.
 */
typedef struct UDFEXFILEENTRY
{
    /** 0x00: The descriptor tag (UDF_TAG_ID_EXTENDED_FILE_ENTRY). */
    UDFTAG          Tag;
    /** 0x10: ICB Tag. */
    UDFICBTAG       IcbTag;
    /** 0x24: User ID (UNIX). */
    uint32_t        uid;
    /** 0x28: Group ID (UNIX). */
    uint32_t        gid;
    /** 0x2c: Permission (UDF_PERM_XXX). */
    uint32_t        fPermissions;
    /** 0x30: Number hard links. */
    uint16_t        cHardlinks;
    /** 0x32: Record format (UDF_REC_FMT_XXX).   */
    uint8_t         uRecordFormat;
    /** 0x33: Record format (UDF_REC_FMT_XXX).   */
    uint8_t         fRecordDisplayAttribs;
    /** 0x34: Record length (in bytes).
     * @note  Must be zero according to the UDF specification. */
    uint32_t        cbRecord;
    /** 0x38: Information length in bytes (file size). */
    uint64_t        cbData;
    /** 0x40: The size of all streams. Same as cbData if no additional streams. */
    uint64_t        cbObject;
    /** 0x48: Number of logical blocks allocated (for file data). */
    uint64_t        cLogicalBlocks;
    /** 0x50: Time of last access (prior to recording the file entry). */
    UDFTIMESTAMP    AccessTime;
    /** 0x5c: Time of last data modification. */
    UDFTIMESTAMP    ModificationTime;
    /** 0x68: Birth (creation) time. */
    UDFTIMESTAMP    BirthTime;
    /** 0x74: Time of last attribute/status modification. */
    UDFTIMESTAMP    ChangeTime;
    /** 0x80: Checkpoint number (defaults to 1). */
    uint32_t        uCheckpoint;
    /** 0x84: Reserved, MBZ. */
    uint32_t        uReserved;
    /** 0x88: Extended attribute information control block location. */
    UDFLONGAD       ExtAttribIcb;
    /** 0x98: Stream directory information control block location. */
    UDFLONGAD       StreamDirIcb;
    /** 0xa8: Implementation identifier (UDF_ENTITY_ID_FE_IMPLEMENTATION). */
    UDFENTITYID     idImplementation;
    /** 0xc8: Unique ID. */
    uint64_t        INodeId;
    /** 0xd0: Length of extended attributes in bytes, multiple of four. */
    uint32_t        cbExtAttribs;
    /** 0xd4: Length of allocation descriptors in bytes, multiple of four. */
    uint32_t        cbAllocDescs;
    /** 0xd8: Two variable sized fields.  First @a cbExtAttribs bytes of extended
     *  attributes, then @a cbAllocDescs bytes of allocation descriptors. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t         abExtAttribs[RT_FLEXIBLE_ARRAY];
} UDFEXFILEENTRY;
AssertCompileMemberOffset(UDFEXFILEENTRY, abExtAttribs, 0xd8);
/** Pointer to an UDF extended file entry. */
typedef UDFEXFILEENTRY *PUDFEXFILEENTRY;
/** Pointer to a const UDF extended file entry. */
typedef UDFEXFILEENTRY const *PCUDFEXFILEENTRY;



/** @name UDF Volume Recognition Sequence (VRS)
 *
 * The recognition sequence usually follows the CD001 descriptor sequence at
 * sector 16 and is there to indicate that the medium (also) contains a UDF file
 * system and which standards are involved.
 *
 * See @ecma167{2,8,31}, @ecma167{2,9,32}, @udf260{2.1.7,25}.
 *
 * @{ */

/** The type value used for all the extended UDF volume descriptors
 * (ISO9660VOLDESCHDR::bDescType). */
#define UDF_EXT_VOL_DESC_TYPE               0
/** The version value used for all the extended UDF volume descriptors
 *  (ISO9660VOLDESCHDR::bDescVersion). */
#define UDF_EXT_VOL_DESC_VERSION            1

/** Standard ID for UDFEXTVOLDESCBEGIN. */
#define UDF_EXT_VOL_DESC_STD_ID_BEGIN   "BEA01"
/** Standard ID for UDFEXTVOLDESCTERM. */
#define UDF_EXT_VOL_DESC_STD_ID_TERM    "TEA01"
/** Standard ID for UDFEXTVOLDESCNSR following ECMA-167 2nd edition. */
#define UDF_EXT_VOL_DESC_STD_ID_NSR_02  "NSR02"
/** Standard ID for UDFEXTVOLDESCNSR following ECMA-167 3rd edition. */
#define UDF_EXT_VOL_DESC_STD_ID_NSR_03  "NSR03"
/** Standard ID for UDFEXTVOLDESCBOOT. */
#define UDF_EXT_VOL_DESC_STD_ID_BOOT    "BOOT2"


/**
 * Begin UDF extended volume descriptor area (@ecma167{2,9.2,33}).
 */
typedef struct UDFEXTVOLDESCBEGIN
{
    /** The volume descriptor header.
     * The standard identifier is UDF_EXT_VOL_DESC_STD_ID_BEGIN. */
    ISO9660VOLDESCHDR   Hdr;
    /** Zero payload. */
    uint8_t             abZero[2041];
} UDFEXTVOLDESCBEGIN;
AssertCompileSize(UDFEXTVOLDESCBEGIN, 2048);
/** Pointer to an UDF extended volume descriptor indicating the start of the
 * extended descriptor area. */
typedef UDFEXTVOLDESCBEGIN *PUDFEXTVOLDESCBEGIN;
/** Pointer to a const UDF extended volume descriptor indicating the start of
 * the extended descriptor area. */
typedef UDFEXTVOLDESCBEGIN const *PCUDFEXTVOLDESCBEGIN;


/**
 * Terminate UDF extended volume descriptor area (@ecma167{2,9.3,33}).
 */
typedef struct UDFEXTVOLDESCTERM
{
    /** The volume descriptor header.
     * The standard identifier is UDF_EXT_VOL_DESC_STD_ID_TERM. */
    ISO9660VOLDESCHDR   Hdr;
    /** Zero payload. */
    uint8_t             abZero[2041];
} UDFEXTVOLDESCTERM;
AssertCompileSize(UDFEXTVOLDESCTERM, 2048);
/** Pointer to an UDF extended volume descriptor indicating the end of the
 * extended descriptor area. */
typedef UDFEXTVOLDESCTERM *PUDFEXTVOLDESCTERM;
/** Pointer to a const UDF extended volume descriptor indicating the end of
 * the extended descriptor area. */
typedef UDFEXTVOLDESCTERM const *PCUDFEXTVOLDESCTERM;


/**
 * UDF NSR extended volume descriptor (@ecma167{3,9.1,50}).
 *
 * This gives the ECMA standard version.
 */
typedef struct UDFEXTVOLDESCNSR
{
    /** The volume descriptor header.
     * The standard identifier is UDF_EXT_VOL_DESC_STD_ID_NSR_02, or
     * UDF_EXT_VOL_DESC_STD_ID_NSR_03. */
    ISO9660VOLDESCHDR   Hdr;
    /** Zero payload. */
    uint8_t             abZero[2041];
} UDFEXTVOLDESCNSR;
AssertCompileSize(UDFEXTVOLDESCNSR, 2048);
/** Pointer to an extended volume descriptor giving the UDF standard version. */
typedef UDFEXTVOLDESCNSR *PUDFEXTVOLDESCNSR;
/** Pointer to a const extended volume descriptor giving the UDF standard version. */
typedef UDFEXTVOLDESCNSR const *PCUDFEXTVOLDESCNSR;


/**
 * UDF boot extended volume descriptor (@ecma167{2,9.4,34}).
 *
 * @note Probably entirely unused.
 */
typedef struct UDFEXTVOLDESCBOOT
{
    /** 0x00: The volume descriptor header.
     * The standard identifier is UDF_EXT_VOL_DESC_STD_ID_BOOT. */
    ISO9660VOLDESCHDR   Hdr;
    /** 0x07: Reserved/alignment, MBZ. */
    uint8_t             bReserved1;
    /** 0x08: The architecture type. */
    UDFENTITYID         ArchType;
    /** 0x28: The boot identifier. */
    UDFENTITYID         idBoot;
    /** 0x48: Logical sector number of load the boot loader from. */
    uint32_t            offBootExtent;
    /** 0x4c: Number of bytes to load. */
    uint32_t            cbBootExtent;
    /** 0x50: The load address (in memory). */
    uint64_t            uLoadAddress;
    /** 0x58: The start address (in memory). */
    uint64_t            uStartAddress;
    /** 0x60: The descriptor creation timestamp. */
    UDFTIMESTAMP        CreationTimestamp;
    /** 0x6c: Flags. */
    uint16_t            fFlags;
    /** 0x6e: Reserved, MBZ. */
    uint8_t             abReserved2[32];
    /** 0x8e: Implementation use. */
    uint8_t             abBootUse[1906];
} UDFEXTVOLDESCBOOT;
AssertCompileSize(UDFEXTVOLDESCBOOT, 2048);
/** Pointer to a boot extended volume descriptor. */
typedef UDFEXTVOLDESCBOOT *PUDFEXTVOLDESCBOOT;
/** Pointer to a const boot extended volume descriptor. */
typedef UDFEXTVOLDESCBOOT const *PCUDFEXTVOLDESCBOOT;

/** @} */


/** @} */

#endif /* !IPRT_INCLUDED_formats_udf_h */

