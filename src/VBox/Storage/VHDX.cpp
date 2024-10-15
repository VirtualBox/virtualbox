/* $Id: VHDX.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VHDX - VHDX Disk image, Core Code.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VD_VHDX
#include <VBox/vd-plugin.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/path.h>
#include <iprt/uuid.h>
#include <iprt/crc.h>

#include "VDBackends.h"
#include "VDBackendsInline.h"


/*********************************************************************************************************************************
*   On disk data structures                                                                                                      *
*********************************************************************************************************************************/

/**
 * VHDX file type identifier.
 */
#pragma pack(1)
typedef struct VhdxFileIdentifier
{
    /** Signature. */
    uint64_t    u64Signature;
    /** Creator ID - UTF-16 string (not neccessarily null terminated). */
    uint16_t    awszCreator[256];
} VhdxFileIdentifier;
#pragma pack()
/** Pointer to an on disk VHDX file type identifier. */
typedef VhdxFileIdentifier *PVhdxFileIdentifier;

/** VHDX file type identifier signature ("vhdxfile"). */
#define VHDX_FILE_IDENTIFIER_SIGNATURE UINT64_C(0x656c696678646876)
/** Start offset of the VHDX file type identifier. */
#define VHDX_FILE_IDENTIFIER_OFFSET    UINT64_C(0)

/**
 * VHDX header.
 */
#pragma pack(1)
typedef struct VhdxHeader
{
    /** Signature. */
    uint32_t    u32Signature;
    /** Checksum. */
    uint32_t    u32Checksum;
    /** Sequence number. */
    uint64_t    u64SequenceNumber;
    /** File write UUID. */
    RTUUID      UuidFileWrite;
    /** Data write UUID. */
    RTUUID      UuidDataWrite;
    /** Log UUID. */
    RTUUID      UuidLog;
    /** Version of the log format. */
    uint16_t    u16LogVersion;
    /** VHDX format version. */
    uint16_t    u16Version;
    /** Length of the log region. */
    uint32_t    u32LogLength;
    /** Start offset of the log offset in the file. */
    uint64_t    u64LogOffset;
    /** Reserved bytes. */
    uint8_t     u8Reserved[4016];
} VhdxHeader;
#pragma pack()
/** Pointer to an on disk VHDX header. */
typedef VhdxHeader *PVhdxHeader;

/** VHDX header signature ("head"). */
#define VHDX_HEADER_SIGNATURE    UINT32_C(0x64616568)
/** Start offset of the first VHDX header. */
#define VHDX_HEADER1_OFFSET      _64K
/** Start offset of the second VHDX header. */
#define VHDX_HEADER2_OFFSET      _128K
/** Current Log format version. */
#define VHDX_HEADER_LOG_VERSION  UINT16_C(0)
/** Current VHDX format version. */
#define VHDX_HEADER_VHDX_VERSION UINT16_C(1)

/**
 * VHDX region table header
 */
#pragma pack(1)
typedef struct VhdxRegionTblHdr
{
    /** Signature. */
    uint32_t    u32Signature;
    /** Checksum. */
    uint32_t    u32Checksum;
    /** Number of region table entries following this header. */
    uint32_t    u32EntryCount;
    /** Reserved. */
    uint32_t    u32Reserved;
} VhdxRegionTblHdr;
#pragma pack()
/** Pointer to an on disk VHDX region table header. */
typedef VhdxRegionTblHdr *PVhdxRegionTblHdr;

/** VHDX region table header signature. */
#define VHDX_REGION_TBL_HDR_SIGNATURE       UINT32_C(0x69676572)
/** Maximum number of entries which can follow. */
#define VHDX_REGION_TBL_HDR_ENTRY_COUNT_MAX UINT32_C(2047)
/** Offset where the region table is stored (192 KB). */
#define VHDX_REGION_TBL_HDR_OFFSET          UINT64_C(196608)
/** Maximum size of the region table. */
#define VHDX_REGION_TBL_SIZE_MAX            _64K

/**
 * VHDX region table entry.
 */
#pragma pack(1)
typedef struct VhdxRegionTblEntry
{
    /** Object UUID. */
    RTUUID      UuidObject;
    /** File offset of the region. */
    uint64_t    u64FileOffset;
    /** Length of the region in bytes. */
    uint32_t    u32Length;
    /** Flags for this object. */
    uint32_t    u32Flags;
} VhdxRegionTblEntry;
#pragma pack()
/** Pointer to an on disk VHDX region table entry. */
typedef struct VhdxRegionTblEntry *PVhdxRegionTblEntry;

/** Flag whether this region is required. */
#define VHDX_REGION_TBL_ENTRY_FLAGS_IS_REQUIRED RT_BIT_32(0)
/** UUID for the BAT region. */
#define VHDX_REGION_TBL_ENTRY_UUID_BAT          "2dc27766-f623-4200-9d64-115e9bfd4a08"
/** UUID for the metadata region. */
#define VHDX_REGION_TBL_ENTRY_UUID_METADATA     "8b7ca206-4790-4b9a-b8fe-575f050f886e"

/**
 * VHDX Log entry header.
 */
#pragma pack(1)
typedef struct VhdxLogEntryHdr
{
    /** Signature. */
    uint32_t    u32Signature;
    /** Checksum. */
    uint32_t    u32Checksum;
    /** Total length of the entry in bytes. */
    uint32_t    u32EntryLength;
    /** Tail of the log entries. */
    uint32_t    u32Tail;
    /** Sequence number. */
    uint64_t    u64SequenceNumber;
    /** Number of descriptors in this log entry. */
    uint32_t    u32DescriptorCount;
    /** Reserved. */
    uint32_t    u32Reserved;
    /** Log UUID. */
    RTUUID      UuidLog;
    /** VHDX file size in bytes while the log entry was written. */
    uint64_t    u64FlushedFileOffset;
    /** File size in bytes all allocated file structures fit into when the
     * log entry was written. */
    uint64_t    u64LastFileOffset;
} VhdxLogEntryHdr;
#pragma pack()
/** Pointer to an on disk VHDX log entry header. */
typedef struct VhdxLogEntryHdr *PVhdxLogEntryHdr;

/** VHDX log entry signature ("loge"). */
#define VHDX_LOG_ENTRY_HEADER_SIGNATURE UINT32_C(0x65676f6c)

/**
 * VHDX log zero descriptor.
 */
#pragma pack(1)
typedef struct VhdxLogZeroDesc
{
    /** Signature of this descriptor. */
    uint32_t    u32ZeroSignature;
    /** Reserved. */
    uint32_t    u32Reserved;
    /** Length of the section to zero. */
    uint64_t    u64ZeroLength;
    /** File offset to write zeros to. */
    uint64_t    u64FileOffset;
    /** Sequence number (must macht the field in the log entry header). */
    uint64_t    u64SequenceNumber;
} VhdxLogZeroDesc;
#pragma pack()
/** Pointer to an on disk VHDX log zero descriptor. */
typedef struct VhdxLogZeroDesc *PVhdxLogZeroDesc;

/** Signature of a VHDX log zero descriptor ("zero"). */
#define VHDX_LOG_ZERO_DESC_SIGNATURE UINT32_C(0x6f72657a)

/**
 * VHDX log data descriptor.
 */
#pragma pack(1)
typedef struct VhdxLogDataDesc
{
    /** Signature of this descriptor. */
    uint32_t    u32DataSignature;
    /** Trailing 4 bytes removed from the update. */
    uint32_t    u32TrailingBytes;
    /** Leading 8 bytes removed from the update. */
    uint64_t    u64LeadingBytes;
    /** File offset to write zeros to. */
    uint64_t    u64FileOffset;
    /** Sequence number (must macht the field in the log entry header). */
    uint64_t    u64SequenceNumber;
} VhdxLogDataDesc;
#pragma pack()
/** Pointer to an on disk VHDX log data descriptor. */
typedef struct VhdxLogDataDesc *PVhdxLogDataDesc;

/** Signature of a VHDX log data descriptor ("desc"). */
#define VHDX_LOG_DATA_DESC_SIGNATURE UINT32_C(0x63736564)

/**
 * VHDX log data sector.
 */
#pragma pack(1)
typedef struct VhdxLogDataSector
{
    /** Signature of the data sector. */
    uint32_t    u32DataSignature;
    /** 4 most significant bytes of the sequence number. */
    uint32_t    u32SequenceHigh;
    /** Raw data associated with the update. */
    uint8_t     u8Data[4084];
    /** 4 least significant bytes of the sequence number. */
    uint32_t    u32SequenceLow;
} VhdxLogDataSector;
#pragma pack()
/** Pointer to an on disk VHDX log data sector. */
typedef VhdxLogDataSector *PVhdxLogDataSector;

/** Signature of a VHDX log data sector ("data"). */
#define VHDX_LOG_DATA_SECTOR_SIGNATURE UINT32_C(0x61746164)

/**
 * VHDX BAT entry.
 */
#pragma pack(1)
typedef struct VhdxBatEntry
{
    /** The BAT entry, contains state and offset. */
    uint64_t    u64BatEntry;
} VhdxBatEntry;
#pragma pack()
typedef VhdxBatEntry *PVhdxBatEntry;

/** Return the BAT state from a given entry. */
#define VHDX_BAT_ENTRY_GET_STATE(bat) ((bat) & UINT64_C(0x7))
/** Get the FileOffsetMB field from a given BAT entry. */
#define VHDX_BAT_ENTRY_GET_FILE_OFFSET_MB(bat) (((bat) & UINT64_C(0xfffffffffff00000)) >> 20)
/** Get a byte offset from the BAT entry. */
#define VHDX_BAT_ENTRY_GET_FILE_OFFSET(bat) (VHDX_BAT_ENTRY_GET_FILE_OFFSET_MB(bat) * (uint64_t)_1M)

/** Block not present and the data is undefined. */
#define VHDX_BAT_ENTRY_PAYLOAD_BLOCK_NOT_PRESENT       (0)
/** Data in this block is undefined. */
#define VHDX_BAT_ENTRY_PAYLOAD_BLOCK_UNDEFINED         (1)
/** Data in this block contains zeros. */
#define VHDX_BAT_ENTRY_PAYLOAD_BLOCK_ZERO              (2)
/** Block was unmapped by the application or system and data is either zero or
 * the data before the block was unmapped. */
#define VHDX_BAT_ENTRY_PAYLOAD_BLOCK_UNMAPPED          (3)
/** Block data is in the file pointed to by the FileOffsetMB field. */
#define VHDX_BAT_ENTRY_PAYLOAD_BLOCK_FULLY_PRESENT     (6)
/** Block is partially present, use sector bitmap to get present sectors. */
#define VHDX_BAT_ENTRY_PAYLOAD_BLOCK_PARTIALLY_PRESENT (7)

/** The sector bitmap block is undefined and not allocated in the file. */
#define VHDX_BAT_ENTRY_SB_BLOCK_NOT_PRESENT            (0)
/** The sector bitmap block is defined at the file location. */
#define VHDX_BAT_ENTRY_SB_BLOCK_PRESENT                (6)

/**
 * VHDX Metadata tabl header.
 */
#pragma pack(1)
typedef struct VhdxMetadataTblHdr
{
    /** Signature. */
    uint64_t    u64Signature;
    /** Reserved. */
    uint16_t    u16Reserved;
    /** Number of entries in the table. */
    uint16_t    u16EntryCount;
    /** Reserved */
    uint32_t    u32Reserved2[5];
} VhdxMetadataTblHdr;
#pragma pack()
/** Pointer to an on disk metadata table header. */
typedef VhdxMetadataTblHdr *PVhdxMetadataTblHdr;

/** Signature of a VHDX metadata table header ("metadata"). */
#define VHDX_METADATA_TBL_HDR_SIGNATURE       UINT64_C(0x617461646174656d)
/** Maximum number of entries the metadata table can have. */
#define VHDX_METADATA_TBL_HDR_ENTRY_COUNT_MAX UINT16_C(2047)

/**
 * VHDX Metadata table entry.
 */
#pragma pack(1)
typedef struct VhdxMetadataTblEntry
{
    /** Item UUID. */
    RTUUID      UuidItem;
    /** Offset of the metadata item. */
    uint32_t    u32Offset;
    /** Length of the metadata item. */
    uint32_t    u32Length;
    /** Flags for the metadata item. */
    uint32_t    u32Flags;
    /** Reserved. */
    uint32_t    u32Reserved;
} VhdxMetadataTblEntry;
#pragma pack()
/** Pointer to an on disk metadata table entry. */
typedef VhdxMetadataTblEntry *PVhdxMetadataTblEntry;

/** FLag whether the metadata item is system or user metadata. */
#define VHDX_METADATA_TBL_ENTRY_FLAGS_IS_USER     RT_BIT_32(0)
/** FLag whether the metadata item is file or virtual disk metadata. */
#define VHDX_METADATA_TBL_ENTRY_FLAGS_IS_VDISK    RT_BIT_32(1)
/** FLag whether the backend must understand the metadata item to load the image. */
#define VHDX_METADATA_TBL_ENTRY_FLAGS_IS_REQUIRED RT_BIT_32(2)

/** File parameters item UUID. */
#define VHDX_METADATA_TBL_ENTRY_ITEM_FILE_PARAMS    "caa16737-fa36-4d43-b3b6-33f0aa44e76b"
/** Virtual disk size item UUID. */
#define VHDX_METADATA_TBL_ENTRY_ITEM_VDISK_SIZE     "2fa54224-cd1b-4876-b211-5dbed83bf4b8"
/** Page 83 UUID. */
#define VHDX_METADATA_TBL_ENTRY_ITEM_PAGE83_DATA    "beca12ab-b2e6-4523-93ef-c309e000c746"
/** Logical sector size UUID. */
#define VHDX_METADATA_TBL_ENTRY_ITEM_LOG_SECT_SIZE  "8141bf1d-a96f-4709-ba47-f233a8faab5f"
/** Physical sector size UUID. */
#define VHDX_METADATA_TBL_ENTRY_ITEM_PHYS_SECT_SIZE "cda348c7-445d-4471-9cc9-e9885251c556"
/** Parent locator UUID. */
#define VHDX_METADATA_TBL_ENTRY_ITEM_PARENT_LOCATOR "a8d35f2d-b30b-454d-abf7-d3d84834ab0c"

/**
 * VHDX File parameters metadata item.
 */
#pragma pack(1)
typedef struct VhdxFileParameters
{
    /** Block size. */
    uint32_t    u32BlockSize;
    /** Flags. */
    uint32_t    u32Flags;
} VhdxFileParameters;
#pragma pack()
/** Pointer to an on disk VHDX file parameters metadata item. */
typedef struct VhdxFileParameters *PVhdxFileParameters;

/** Flag whether to leave blocks allocated in the file or if it is possible to unmap them. */
#define VHDX_FILE_PARAMETERS_FLAGS_LEAVE_BLOCKS_ALLOCATED RT_BIT_32(0)
/** Flag whether this file has a parent VHDX file. */
#define VHDX_FILE_PARAMETERS_FLAGS_HAS_PARENT             RT_BIT_32(1)

/**
 * VHDX virtual disk size metadata item.
 */
#pragma pack(1)
typedef struct VhdxVDiskSize
{
    /** Virtual disk size. */
    uint64_t    u64VDiskSize;
} VhdxVDiskSize;
#pragma pack()
/** Pointer to an on disk VHDX virtual disk size metadata item. */
typedef struct VhdxVDiskSize *PVhdxVDiskSize;

/**
 * VHDX page 83 data metadata item.
 */
#pragma pack(1)
typedef struct VhdxPage83Data
{
    /** UUID for the SCSI device. */
    RTUUID      UuidPage83Data;
} VhdxPage83Data;
#pragma pack()
/** Pointer to an on disk VHDX vpage 83 data metadata item. */
typedef struct VhdxPage83Data *PVhdxPage83Data;

/**
 * VHDX virtual disk logical sector size.
 */
#pragma pack(1)
typedef struct VhdxVDiskLogicalSectorSize
{
    /** Logical sector size. */
    uint32_t    u32LogicalSectorSize;
} VhdxVDiskLogicalSectorSize;
#pragma pack()
/** Pointer to an on disk VHDX virtual disk logical sector size metadata item. */
typedef struct VhdxVDiskLogicalSectorSize *PVhdxVDiskLogicalSectorSize;

/**
 * VHDX virtual disk physical sector size.
 */
#pragma pack(1)
typedef struct VhdxVDiskPhysicalSectorSize
{
    /** Physical sector size. */
    uint64_t    u64PhysicalSectorSize;
} VhdxVDiskPhysicalSectorSize;
#pragma pack()
/** Pointer to an on disk VHDX virtual disk physical sector size metadata item. */
typedef struct VhdxVDiskPhysicalSectorSize *PVhdxVDiskPhysicalSectorSize;

/**
 * VHDX parent locator header.
 */
#pragma pack(1)
typedef struct VhdxParentLocatorHeader
{
    /** Locator type UUID. */
    RTUUID      UuidLocatorType;
    /** Reserved. */
    uint16_t    u16Reserved;
    /** Number of key value pairs. */
    uint16_t    u16KeyValueCount;
} VhdxParentLocatorHeader;
#pragma pack()
/** Pointer to an on disk VHDX parent locator header metadata item. */
typedef struct VhdxParentLocatorHeader *PVhdxParentLocatorHeader;

/** VHDX parent locator type. */
#define VHDX_PARENT_LOCATOR_TYPE_VHDX "b04aefb7-d19e-4a81-b789-25b8e9445913"

/**
 * VHDX parent locator entry.
 */
#pragma pack(1)
typedef struct VhdxParentLocatorEntry
{
    /** Offset of the key. */
    uint32_t    u32KeyOffset;
    /** Offset of the value. */
    uint32_t    u32ValueOffset;
    /** Length of the key. */
    uint16_t    u16KeyLength;
    /** Length of the value. */
    uint16_t    u16ValueLength;
} VhdxParentLocatorEntry;
#pragma pack()
/** Pointer to an on disk VHDX parent locator entry. */
typedef struct VhdxParentLocatorEntry *PVhdxParentLocatorEntry;


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

typedef enum VHDXMETADATAITEM
{
    VHDXMETADATAITEM_UNKNOWN = 0,
    VHDXMETADATAITEM_FILE_PARAMS,
    VHDXMETADATAITEM_VDISK_SIZE,
    VHDXMETADATAITEM_PAGE83_DATA,
    VHDXMETADATAITEM_LOGICAL_SECTOR_SIZE,
    VHDXMETADATAITEM_PHYSICAL_SECTOR_SIZE,
    VHDXMETADATAITEM_PARENT_LOCATOR,
    VHDXMETADATAITEM_32BIT_HACK = 0x7fffffff
} VHDXMETADATAITEM;

/**
 * Table to validate the metadata item UUIDs and the flags.
 */
typedef struct VHDXMETADATAITEMPROPS
{
    /** Item UUID. */
    const char          *pszItemUuid;
    /** Flag whether this is a user or system metadata item. */
    bool                 fIsUser;
    /** Flag whether this is a virtual disk or file metadata item. */
    bool                 fIsVDisk;
    /** Flag whether this metadata item is required to load the file. */
    bool                 fIsRequired;
    /** Metadata item enum associated with this UUID. */
    VHDXMETADATAITEM     enmMetadataItem;
} VHDXMETADATAITEMPROPS;

/**
 * VHDX image data structure.
 */
typedef struct VHDXIMAGE
{
    /** Image name. */
    const char         *pszFilename;
    /** Storage handle. */
    PVDIOSTORAGE        pStorage;

    /** Pointer to the per-disk VD interface list. */
    PVDINTERFACE        pVDIfsDisk;
    /** Pointer to the per-image VD interface list. */
    PVDINTERFACE        pVDIfsImage;
    /** Error interface. */
    PVDINTERFACEERROR   pIfError;
    /** I/O interface. */
    PVDINTERFACEIOINT   pIfIo;

    /** Open flags passed by VBoxHD layer. */
    unsigned            uOpenFlags;
    /** Image flags defined during creation or determined during open. */
    unsigned            uImageFlags;
    /** Version of the VHDX image format. */
    unsigned            uVersion;
    /** Total size of the image. */
    uint64_t            cbSize;
    /** Logical sector size of the image. */
    uint32_t            cbLogicalSector;
    /** Block size of the image. */
    size_t              cbBlock;
    /** Physical geometry of this image. */
    VDGEOMETRY          PCHSGeometry;
    /** Logical geometry of this image. */
    VDGEOMETRY          LCHSGeometry;

    /** The BAT. */
    PVhdxBatEntry       paBat;
    /** Chunk ratio. */
    uint32_t            uChunkRatio;
    /** The static region list. */
    VDREGIONLIST        RegionList;
} VHDXIMAGE, *PVHDXIMAGE;

/**
 * Endianess conversion direction.
 */
typedef enum VHDXECONV
{
    /** Host to file endianess. */
    VHDXECONV_H2F = 0,
    /** File to host endianess. */
    VHDXECONV_F2H
} VHDXECONV;

/** Macros for endianess conversion. */
#define SET_ENDIAN_U16(u16) (enmConv == VHDXECONV_H2F ? RT_H2LE_U16(u16) : RT_LE2H_U16(u16))
#define SET_ENDIAN_U32(u32) (enmConv == VHDXECONV_H2F ? RT_H2LE_U32(u32) : RT_LE2H_U32(u32))
#define SET_ENDIAN_U64(u64) (enmConv == VHDXECONV_H2F ? RT_H2LE_U64(u64) : RT_LE2H_U64(u64))


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * NULL-terminated array of supported file extensions.
 */
static const VDFILEEXTENSION s_aVhdxFileExtensions[] =
{
    {"vhdx", VDTYPE_HDD},
    {NULL, VDTYPE_INVALID}
};

/**
 * Static table to verify the metadata item properties and the flags.
 */
static const VHDXMETADATAITEMPROPS s_aVhdxMetadataItemProps[] =
{
    /* pcszItemUuid                               fIsUser, fIsVDisk, fIsRequired, enmMetadataItem */
    {VHDX_METADATA_TBL_ENTRY_ITEM_FILE_PARAMS,    false,   false,    true,        VHDXMETADATAITEM_FILE_PARAMS},
    {VHDX_METADATA_TBL_ENTRY_ITEM_VDISK_SIZE,     false,   true,     true,        VHDXMETADATAITEM_VDISK_SIZE},
    {VHDX_METADATA_TBL_ENTRY_ITEM_PAGE83_DATA,    false,   true,     true,        VHDXMETADATAITEM_PAGE83_DATA},
    {VHDX_METADATA_TBL_ENTRY_ITEM_LOG_SECT_SIZE,  false,   true,     true,        VHDXMETADATAITEM_LOGICAL_SECTOR_SIZE},
    {VHDX_METADATA_TBL_ENTRY_ITEM_PHYS_SECT_SIZE, false,   true,     true,        VHDXMETADATAITEM_PHYSICAL_SECTOR_SIZE},
    {VHDX_METADATA_TBL_ENTRY_ITEM_PARENT_LOCATOR, false,   false,    true,        VHDXMETADATAITEM_PARENT_LOCATOR}
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Converts the file identifier between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pFileIdentifierConv Where to store the converted file identifier.
 * @param   pFileIdentifier     The file identifier to convert.
 *
 * @note It is safe to use the same pointer for pFileIdentifierConv and pFileIdentifier.
 */
DECLINLINE(void) vhdxConvFileIdentifierEndianess(VHDXECONV enmConv, PVhdxFileIdentifier pFileIdentifierConv,
                                                 PVhdxFileIdentifier pFileIdentifier)
{
    pFileIdentifierConv->u64Signature = SET_ENDIAN_U64(pFileIdentifier->u64Signature);
    for (unsigned i = 0; i < RT_ELEMENTS(pFileIdentifierConv->awszCreator); i++)
        pFileIdentifierConv->awszCreator[i] = SET_ENDIAN_U16(pFileIdentifier->awszCreator[i]);
}

/**
 * Converts a UUID between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pUuidConv           Where to store the converted UUID.
 * @param   pUuid               The UUID to convert.
 *
 * @note It is safe to use the same pointer for pUuidConv and pUuid.
 */
DECLINLINE(void) vhdxConvUuidEndianess(VHDXECONV enmConv, PRTUUID pUuidConv, PRTUUID pUuid)
{
    RT_NOREF1(enmConv);
#if 1
    /** @todo r=andy Code looks temporary disabled to me, fixes strict release builds:
     *        "accessing 16 bytes at offsets 0 and 0 overlaps 16 bytes at offset 0 [-Werror=restrict]" */
    RTUUID uuidTmp;
    memcpy(&uuidTmp,  pUuid,    sizeof(RTUUID));
    memcpy(pUuidConv, &uuidTmp, sizeof(RTUUID));
#else
    pUuidConv->Gen.u32TimeLow              = SET_ENDIAN_U32(pUuid->Gen.u32TimeLow);
    pUuidConv->Gen.u16TimeMid              = SET_ENDIAN_U16(pUuid->Gen.u16TimeMid);
    pUuidConv->Gen.u16TimeHiAndVersion     = SET_ENDIAN_U16(pUuid->Gen.u16TimeHiAndVersion);
    pUuidConv->Gen.u8ClockSeqHiAndReserved = pUuid->Gen.u8ClockSeqHiAndReserved;
    pUuidConv->Gen.u8ClockSeqLow           = pUuid->Gen.u8ClockSeqLow;
    for (unsigned i = 0; i < RT_ELEMENTS(pUuidConv->Gen.au8Node); i++)
        pUuidConv->Gen.au8Node[i] = pUuid->Gen.au8Node[i];
#endif
}

/**
 * Converts a VHDX header between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pHdrConv            Where to store the converted header.
 * @param   pHdr                The VHDX header to convert.
 *
 * @note It is safe to use the same pointer for pHdrConv and pHdr.
 */
DECLINLINE(void) vhdxConvHeaderEndianess(VHDXECONV enmConv, PVhdxHeader pHdrConv, PVhdxHeader pHdr)
{
    pHdrConv->u32Signature      = SET_ENDIAN_U32(pHdr->u32Signature);
    pHdrConv->u32Checksum       = SET_ENDIAN_U32(pHdr->u32Checksum);
    pHdrConv->u64SequenceNumber = SET_ENDIAN_U64(pHdr->u64SequenceNumber);
    vhdxConvUuidEndianess(enmConv, &pHdrConv->UuidFileWrite, &pHdrConv->UuidFileWrite);
    vhdxConvUuidEndianess(enmConv, &pHdrConv->UuidDataWrite, &pHdrConv->UuidDataWrite);
    vhdxConvUuidEndianess(enmConv, &pHdrConv->UuidLog, &pHdrConv->UuidLog);
    pHdrConv->u16LogVersion     = SET_ENDIAN_U16(pHdr->u16LogVersion);
    pHdrConv->u16Version        = SET_ENDIAN_U16(pHdr->u16Version);
    pHdrConv->u32LogLength      = SET_ENDIAN_U32(pHdr->u32LogLength);
    pHdrConv->u64LogOffset      = SET_ENDIAN_U64(pHdr->u64LogOffset);
}

/**
 * Converts a VHDX region table header between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pRegTblHdrConv      Where to store the converted header.
 * @param   pRegTblHdr          The VHDX region table header to convert.
 *
 * @note It is safe to use the same pointer for pRegTblHdrConv and pRegTblHdr.
 */
DECLINLINE(void) vhdxConvRegionTblHdrEndianess(VHDXECONV enmConv, PVhdxRegionTblHdr pRegTblHdrConv,
                                               PVhdxRegionTblHdr pRegTblHdr)
{
    pRegTblHdrConv->u32Signature  = SET_ENDIAN_U32(pRegTblHdr->u32Signature);
    pRegTblHdrConv->u32Checksum   = SET_ENDIAN_U32(pRegTblHdr->u32Checksum);
    pRegTblHdrConv->u32EntryCount = SET_ENDIAN_U32(pRegTblHdr->u32EntryCount);
    pRegTblHdrConv->u32Reserved   = SET_ENDIAN_U32(pRegTblHdr->u32Reserved);
}

/**
 * Converts a VHDX region table entry between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pRegTblEntConv      Where to store the converted region table entry.
 * @param   pRegTblEnt          The VHDX region table entry to convert.
 *
 * @note It is safe to use the same pointer for pRegTblEntConv and pRegTblEnt.
 */
DECLINLINE(void) vhdxConvRegionTblEntryEndianess(VHDXECONV enmConv, PVhdxRegionTblEntry pRegTblEntConv,
                                                 PVhdxRegionTblEntry pRegTblEnt)
{
    vhdxConvUuidEndianess(enmConv, &pRegTblEntConv->UuidObject, &pRegTblEnt->UuidObject);
    pRegTblEntConv->u64FileOffset = SET_ENDIAN_U64(pRegTblEnt->u64FileOffset);
    pRegTblEntConv->u32Length     = SET_ENDIAN_U32(pRegTblEnt->u32Length);
    pRegTblEntConv->u32Flags      = SET_ENDIAN_U32(pRegTblEnt->u32Flags);
}

#if 0 /* unused */

/**
 * Converts a VHDX log entry header between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pLogEntryHdrConv    Where to store the converted log entry header.
 * @param   pLogEntryHdr        The VHDX log entry header to convert.
 *
 * @note It is safe to use the same pointer for pLogEntryHdrConv and pLogEntryHdr.
 */
DECLINLINE(void) vhdxConvLogEntryHdrEndianess(VHDXECONV enmConv, PVhdxLogEntryHdr pLogEntryHdrConv,
                                              PVhdxLogEntryHdr pLogEntryHdr)
{
    pLogEntryHdrConv->u32Signature         = SET_ENDIAN_U32(pLogEntryHdr->u32Signature);
    pLogEntryHdrConv->u32Checksum          = SET_ENDIAN_U32(pLogEntryHdr->u32Checksum);
    pLogEntryHdrConv->u32EntryLength       = SET_ENDIAN_U32(pLogEntryHdr->u32EntryLength);
    pLogEntryHdrConv->u32Tail              = SET_ENDIAN_U32(pLogEntryHdr->u32Tail);
    pLogEntryHdrConv->u64SequenceNumber    = SET_ENDIAN_U64(pLogEntryHdr->u64SequenceNumber);
    pLogEntryHdrConv->u32DescriptorCount   = SET_ENDIAN_U32(pLogEntryHdr->u32DescriptorCount);
    pLogEntryHdrConv->u32Reserved          = SET_ENDIAN_U32(pLogEntryHdr->u32Reserved);
    vhdxConvUuidEndianess(enmConv, &pLogEntryHdrConv->UuidLog, &pLogEntryHdr->UuidLog);
    pLogEntryHdrConv->u64FlushedFileOffset = SET_ENDIAN_U64(pLogEntryHdr->u64FlushedFileOffset);
}

/**
 * Converts a VHDX log zero descriptor between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pLogZeroDescConv    Where to store the converted log zero descriptor.
 * @param   pLogZeroDesc        The VHDX log zero descriptor to convert.
 *
 * @note It is safe to use the same pointer for pLogZeroDescConv and pLogZeroDesc.
 */
DECLINLINE(void) vhdxConvLogZeroDescEndianess(VHDXECONV enmConv, PVhdxLogZeroDesc pLogZeroDescConv,
                                              PVhdxLogZeroDesc pLogZeroDesc)
{
    pLogZeroDescConv->u32ZeroSignature  = SET_ENDIAN_U32(pLogZeroDesc->u32ZeroSignature);
    pLogZeroDescConv->u32Reserved       = SET_ENDIAN_U32(pLogZeroDesc->u32Reserved);
    pLogZeroDescConv->u64ZeroLength     = SET_ENDIAN_U64(pLogZeroDesc->u64ZeroLength);
    pLogZeroDescConv->u64FileOffset     = SET_ENDIAN_U64(pLogZeroDesc->u64FileOffset);
    pLogZeroDescConv->u64SequenceNumber = SET_ENDIAN_U64(pLogZeroDesc->u64SequenceNumber);
}


/**
 * Converts a VHDX log data descriptor between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pLogDataDescConv    Where to store the converted log data descriptor.
 * @param   pLogDataDesc        The VHDX log data descriptor to convert.
 *
 * @note It is safe to use the same pointer for pLogDataDescConv and pLogDataDesc.
 */
DECLINLINE(void) vhdxConvLogDataDescEndianess(VHDXECONV enmConv, PVhdxLogDataDesc pLogDataDescConv,
                                              PVhdxLogDataDesc pLogDataDesc)
{
    pLogDataDescConv->u32DataSignature  = SET_ENDIAN_U32(pLogDataDesc->u32DataSignature);
    pLogDataDescConv->u32TrailingBytes  = SET_ENDIAN_U32(pLogDataDesc->u32TrailingBytes);
    pLogDataDescConv->u64LeadingBytes   = SET_ENDIAN_U64(pLogDataDesc->u64LeadingBytes);
    pLogDataDescConv->u64FileOffset     = SET_ENDIAN_U64(pLogDataDesc->u64FileOffset);
    pLogDataDescConv->u64SequenceNumber = SET_ENDIAN_U64(pLogDataDesc->u64SequenceNumber);
}


/**
 * Converts a VHDX log data sector between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pLogDataSectorConv  Where to store the converted log data sector.
 * @param   pLogDataSector      The VHDX log data sector to convert.
 *
 * @note It is safe to use the same pointer for pLogDataSectorConv and pLogDataSector.
 */
DECLINLINE(void) vhdxConvLogDataSectorEndianess(VHDXECONV enmConv, PVhdxLogDataSector pLogDataSectorConv,
                                                PVhdxLogDataSector pLogDataSector)
{
    pLogDataSectorConv->u32DataSignature = SET_ENDIAN_U32(pLogDataSector->u32DataSignature);
    pLogDataSectorConv->u32SequenceHigh  = SET_ENDIAN_U32(pLogDataSector->u32SequenceHigh);
    pLogDataSectorConv->u32SequenceLow   = SET_ENDIAN_U32(pLogDataSector->u32SequenceLow);
}

#endif /* unused */

/**
 * Converts a BAT between file and host endianess.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   paBatEntriesConv    Where to store the converted BAT.
 * @param   paBatEntries        The VHDX BAT to convert.
 * @param   cBatEntries         Number of entries in the BAT.
 *
 * @note It is safe to use the same pointer for paBatEntriesConv and paBatEntries.
 */
DECLINLINE(void) vhdxConvBatTableEndianess(VHDXECONV enmConv, PVhdxBatEntry paBatEntriesConv,
                                           PVhdxBatEntry paBatEntries, uint32_t cBatEntries)
{
    for (uint32_t i = 0; i < cBatEntries; i++)
        paBatEntriesConv[i].u64BatEntry = SET_ENDIAN_U64(paBatEntries[i].u64BatEntry);
}

/**
 * Converts a VHDX metadata table header between file and host endianness.
 *
 * @param   enmConv             Direction of the conversion.
 * @param   pMetadataTblHdrConv Where to store the converted metadata table header.
 * @param   pMetadataTblHdr     The VHDX metadata table header to convert.
 *
 * @note It is safe to use the same pointer for pMetadataTblHdrConv and pMetadataTblHdr.
 */
DECLINLINE(void) vhdxConvMetadataTblHdrEndianess(VHDXECONV enmConv, PVhdxMetadataTblHdr pMetadataTblHdrConv,
                                                 PVhdxMetadataTblHdr pMetadataTblHdr)
{
    pMetadataTblHdrConv->u64Signature  = SET_ENDIAN_U64(pMetadataTblHdr->u64Signature);
    pMetadataTblHdrConv->u16Reserved   = SET_ENDIAN_U16(pMetadataTblHdr->u16Reserved);
    pMetadataTblHdrConv->u16EntryCount = SET_ENDIAN_U16(pMetadataTblHdr->u16EntryCount);
    for (unsigned i = 0; i < RT_ELEMENTS(pMetadataTblHdr->u32Reserved2); i++)
        pMetadataTblHdrConv->u32Reserved2[i] = SET_ENDIAN_U32(pMetadataTblHdr->u32Reserved2[i]);
}

/**
 * Converts a VHDX metadata table entry between file and host endianness.
 *
 * @param   enmConv               Direction of the conversion.
 * @param   pMetadataTblEntryConv Where to store the converted metadata table entry.
 * @param   pMetadataTblEntry     The VHDX metadata table entry to convert.
 *
 * @note It is safe to use the same pointer for pMetadataTblEntryConv and pMetadataTblEntry.
 */
DECLINLINE(void) vhdxConvMetadataTblEntryEndianess(VHDXECONV enmConv, PVhdxMetadataTblEntry pMetadataTblEntryConv,
                                                   PVhdxMetadataTblEntry pMetadataTblEntry)
{
    vhdxConvUuidEndianess(enmConv, &pMetadataTblEntryConv->UuidItem, &pMetadataTblEntry->UuidItem);
    pMetadataTblEntryConv->u32Offset   = SET_ENDIAN_U32(pMetadataTblEntry->u32Offset);
    pMetadataTblEntryConv->u32Length   = SET_ENDIAN_U32(pMetadataTblEntry->u32Length);
    pMetadataTblEntryConv->u32Flags    = SET_ENDIAN_U32(pMetadataTblEntry->u32Flags);
    pMetadataTblEntryConv->u32Reserved = SET_ENDIAN_U32(pMetadataTblEntry->u32Reserved);
}

/**
 * Converts a VHDX file parameters item between file and host endianness.
 *
 * @param   enmConv               Direction of the conversion.
 * @param   pFileParamsConv       Where to store the converted file parameters item entry.
 * @param   pFileParams           The VHDX file parameters item to convert.
 *
 * @note It is safe to use the same pointer for pFileParamsConv and pFileParams.
 */
DECLINLINE(void) vhdxConvFileParamsEndianess(VHDXECONV enmConv, PVhdxFileParameters pFileParamsConv,
                                             PVhdxFileParameters pFileParams)
{
    pFileParamsConv->u32BlockSize = SET_ENDIAN_U32(pFileParams->u32BlockSize);
    pFileParamsConv->u32Flags     = SET_ENDIAN_U32(pFileParams->u32Flags);
}

/**
 * Converts a VHDX virtual disk size item between file and host endianness.
 *
 * @param   enmConv               Direction of the conversion.
 * @param   pVDiskSizeConv        Where to store the converted virtual disk size item entry.
 * @param   pVDiskSize            The VHDX virtual disk size item to convert.
 *
 * @note It is safe to use the same pointer for pVDiskSizeConv and pVDiskSize.
 */
DECLINLINE(void) vhdxConvVDiskSizeEndianess(VHDXECONV enmConv, PVhdxVDiskSize pVDiskSizeConv,
                                            PVhdxVDiskSize pVDiskSize)
{
    pVDiskSizeConv->u64VDiskSize  = SET_ENDIAN_U64(pVDiskSize->u64VDiskSize);
}

#if 0 /* unused */

/**
 * Converts a VHDX page 83 data item between file and host endianness.
 *
 * @param   enmConv               Direction of the conversion.
 * @param   pPage83DataConv       Where to store the converted page 83 data item entry.
 * @param   pPage83Data           The VHDX page 83 data item to convert.
 *
 * @note It is safe to use the same pointer for pPage83DataConv and pPage83Data.
 */
DECLINLINE(void) vhdxConvPage83DataEndianess(VHDXECONV enmConv, PVhdxPage83Data pPage83DataConv,
                                             PVhdxPage83Data pPage83Data)
{
    vhdxConvUuidEndianess(enmConv, &pPage83DataConv->UuidPage83Data, &pPage83Data->UuidPage83Data);
}
#endif /* unused */

/**
 * Converts a VHDX logical sector size item between file and host endianness.
 *
 * @param   enmConv               Direction of the conversion.
 * @param   pVDiskLogSectSizeConv Where to store the converted logical sector size item entry.
 * @param   pVDiskLogSectSize     The VHDX logical sector size item to convert.
 *
 * @note It is safe to use the same pointer for pVDiskLogSectSizeConv and pVDiskLogSectSize.
 */
DECLINLINE(void) vhdxConvVDiskLogSectSizeEndianess(VHDXECONV enmConv, PVhdxVDiskLogicalSectorSize pVDiskLogSectSizeConv,
                                                   PVhdxVDiskLogicalSectorSize pVDiskLogSectSize)
{
    pVDiskLogSectSizeConv->u32LogicalSectorSize = SET_ENDIAN_U32(pVDiskLogSectSize->u32LogicalSectorSize);
}

#if 0 /* unused */

/**
 * Converts a VHDX physical sector size item between file and host endianness.
 *
 * @param   enmConv                Direction of the conversion.
 * @param   pVDiskPhysSectSizeConv Where to store the converted physical sector size item entry.
 * @param   pVDiskPhysSectSize     The VHDX physical sector size item to convert.
 *
 * @note It is safe to use the same pointer for pVDiskPhysSectSizeConv and pVDiskPhysSectSize.
 */
DECLINLINE(void) vhdxConvVDiskPhysSectSizeEndianess(VHDXECONV enmConv, PVhdxVDiskPhysicalSectorSize pVDiskPhysSectSizeConv,
                                                    PVhdxVDiskPhysicalSectorSize pVDiskPhysSectSize)
{
    pVDiskPhysSectSizeConv->u64PhysicalSectorSize = SET_ENDIAN_U64(pVDiskPhysSectSize->u64PhysicalSectorSize);
}


/**
 * Converts a VHDX parent locator header item between file and host endianness.
 *
 * @param   enmConv                Direction of the conversion.
 * @param   pParentLocatorHdrConv  Where to store the converted parent locator header item entry.
 * @param   pParentLocatorHdr      The VHDX parent locator header item to convert.
 *
 * @note It is safe to use the same pointer for pParentLocatorHdrConv and pParentLocatorHdr.
 */
DECLINLINE(void) vhdxConvParentLocatorHeaderEndianness(VHDXECONV enmConv, PVhdxParentLocatorHeader pParentLocatorHdrConv,
                                                       PVhdxParentLocatorHeader pParentLocatorHdr)
{
    vhdxConvUuidEndianess(enmConv, &pParentLocatorHdrConv->UuidLocatorType, &pParentLocatorHdr->UuidLocatorType);
    pParentLocatorHdrConv->u16Reserved      = SET_ENDIAN_U16(pParentLocatorHdr->u16Reserved);
    pParentLocatorHdrConv->u16KeyValueCount = SET_ENDIAN_U16(pParentLocatorHdr->u16KeyValueCount);
}


/**
 * Converts a VHDX parent locator entry between file and host endianness.
 *
 * @param   enmConv                 Direction of the conversion.
 * @param   pParentLocatorEntryConv Where to store the converted parent locator entry.
 * @param   pParentLocatorEntry     The VHDX parent locator entry to convert.
 *
 * @note It is safe to use the same pointer for pParentLocatorEntryConv and pParentLocatorEntry.
 */
DECLINLINE(void) vhdxConvParentLocatorEntryEndianess(VHDXECONV enmConv, PVhdxParentLocatorEntry pParentLocatorEntryConv,
                                                     PVhdxParentLocatorEntry pParentLocatorEntry)
{
    pParentLocatorEntryConv->u32KeyOffset   = SET_ENDIAN_U32(pParentLocatorEntry->u32KeyOffset);
    pParentLocatorEntryConv->u32ValueOffset = SET_ENDIAN_U32(pParentLocatorEntry->u32ValueOffset);
    pParentLocatorEntryConv->u16KeyLength   = SET_ENDIAN_U16(pParentLocatorEntry->u16KeyLength);
    pParentLocatorEntryConv->u16ValueLength = SET_ENDIAN_U16(pParentLocatorEntry->u16ValueLength);
}

#endif /* unused */

/**
 * Internal. Free all allocated space for representing an image except pImage,
 * and optionally delete the image from disk.
 */
static int vhdxFreeImage(PVHDXIMAGE pImage, bool fDelete)
{
    int rc = VINF_SUCCESS;

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pImage)
    {
        if (pImage->pStorage)
        {
            rc = vdIfIoIntFileClose(pImage->pIfIo, pImage->pStorage);
            pImage->pStorage = NULL;
        }

        if (pImage->paBat)
        {
            RTMemFree(pImage->paBat);
            pImage->paBat = NULL;
        }

        if (fDelete && pImage->pszFilename)
            vdIfIoIntFileDelete(pImage->pIfIo, pImage->pszFilename);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Loads all required fields from the given VHDX header.
 * The header must be converted to the host endianess and validated already.
 *
 * @returns VBox status code.
 * @param   pImage    Image instance data.
 * @param   pHdr      The header to load.
 */
static int vhdxLoadHeader(PVHDXIMAGE pImage, PVhdxHeader pHdr)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p pHdr=%#p\n", pImage, pHdr));

    /*
     * Most fields in the header are not required because the backend implements
     * readonly access only so far.
     * We just have to check that the log is empty, we have to refuse to load the
     * image otherwsie because replaying the log is not implemented.
     */
    if (pHdr->u16Version == VHDX_HEADER_VHDX_VERSION)
    {
        /* Check that the log UUID is zero. */
        pImage->uVersion = pHdr->u16Version;
        if (!RTUuidIsNull(&pHdr->UuidLog))
            rc = vdIfError(pImage->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                           "VHDX: Image \'%s\' has a non empty log which is not supported",
                           pImage->pszFilename);
    }
    else
        rc = vdIfError(pImage->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                       "VHDX: Image \'%s\' uses an unsupported version (%u) of the VHDX format",
                       pImage->pszFilename, pHdr->u16Version);

    LogFlowFunc(("return rc=%Rrc\n", rc));
    return rc;
}

/**
 * Determines the current header and loads it.
 *
 * @returns VBox status code.
 * @param   pImage    Image instance data.
 */
static int vhdxFindAndLoadCurrentHeader(PVHDXIMAGE pImage)
{
    PVhdxHeader pHdr1, pHdr2;
    uint32_t u32ChkSum = 0;
    uint32_t u32ChkSumSaved = 0;
    bool fHdr1Valid = false;
    bool fHdr2Valid = false;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p\n", pImage));

    /*
     * The VHDX format defines two headers at different offsets to provide failure
     * consistency. Only one header is current. This can be determined using the
     * sequence number and checksum fields in the header.
     */
    pHdr1 = (PVhdxHeader)RTMemAllocZ(sizeof(VhdxHeader));
    pHdr2 = (PVhdxHeader)RTMemAllocZ(sizeof(VhdxHeader));

    if (pHdr1 && pHdr2)
    {
        /* Read the first header. */
        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, VHDX_HEADER1_OFFSET,
                                   pHdr1, sizeof(*pHdr1));
        if (RT_SUCCESS(rc))
        {
            vhdxConvHeaderEndianess(VHDXECONV_F2H, pHdr1, pHdr1);

            /* Validate checksum. */
            u32ChkSumSaved = pHdr1->u32Checksum;
            pHdr1->u32Checksum = 0;
            u32ChkSum = RTCrc32C(pHdr1, sizeof(VhdxHeader));

            if (   pHdr1->u32Signature == VHDX_HEADER_SIGNATURE
                && u32ChkSum == u32ChkSumSaved)
                fHdr1Valid = true;
        }

        /* Try to read the second header in any case (even if reading the first failed). */
        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, VHDX_HEADER2_OFFSET,
                                   pHdr2, sizeof(*pHdr2));
        if (RT_SUCCESS(rc))
        {
            vhdxConvHeaderEndianess(VHDXECONV_F2H, pHdr2, pHdr2);

            /* Validate checksum. */
            u32ChkSumSaved = pHdr2->u32Checksum;
            pHdr2->u32Checksum = 0;
            u32ChkSum = RTCrc32C(pHdr2, sizeof(VhdxHeader));

            if (   pHdr2->u32Signature == VHDX_HEADER_SIGNATURE
                && u32ChkSum == u32ChkSumSaved)
                fHdr2Valid = true;
        }

        /* Determine the current header. */
        if (fHdr1Valid != fHdr2Valid)
        {
            /* Only one header is valid - use it. */
            rc = vhdxLoadHeader(pImage, fHdr1Valid ? pHdr1 : pHdr2);
        }
        else if (!fHdr1Valid && !fHdr2Valid)
        {
            /* Crap, both headers are corrupt, refuse to load the image. */
            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                           "VHDX: Can not load the image because both headers are corrupt");
        }
        else
        {
            /* Both headers are valid. Use the sequence number to find the current one. */
            if (pHdr1->u64SequenceNumber > pHdr2->u64SequenceNumber)
                rc = vhdxLoadHeader(pImage, pHdr1);
            else
                rc = vhdxLoadHeader(pImage, pHdr2);
        }
    }
    else
        rc = vdIfError(pImage->pIfError, VERR_NO_MEMORY, RT_SRC_POS,
                       "VHDX: Out of memory while allocating memory for the header");

    if (pHdr1)
        RTMemFree(pHdr1);
    if (pHdr2)
        RTMemFree(pHdr2);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Loads the BAT region.
 *
 * @returns VBox status code.
 * @param   pImage    Image instance data.
 * @param   offRegion Start offset of the region.
 * @param   cbRegion  Size of the region.
 */
static int vhdxLoadBatRegion(PVHDXIMAGE pImage, uint64_t offRegion,
                             size_t cbRegion)
{
    int rc = VINF_SUCCESS;
    uint32_t cDataBlocks;
    uint32_t uChunkRatio;
    uint32_t cBatEntries;
    uint32_t cbBatEntries;
    PVhdxBatEntry paBatEntries = NULL;

    LogFlowFunc(("pImage=%#p\n", pImage));

    /* Calculate required values first. */
    uint64_t uChunkRatio64 = (RT_BIT_64(23) * pImage->cbLogicalSector) / pImage->cbBlock;
    uChunkRatio = (uint32_t)uChunkRatio64; Assert(uChunkRatio == uChunkRatio64);
    uint64_t cDataBlocks64 = pImage->cbSize / pImage->cbBlock;
    cDataBlocks = (uint32_t)cDataBlocks64; Assert(cDataBlocks == cDataBlocks64);

    if (pImage->cbSize % pImage->cbBlock)
        cDataBlocks++;

    cBatEntries = cDataBlocks + (cDataBlocks - 1)/uChunkRatio;
    cbBatEntries = cBatEntries * sizeof(VhdxBatEntry);

    if (cbBatEntries <= cbRegion)
    {
        /*
         * Load the complete BAT region first, convert to host endianess and process
         * it afterwards. The SB entries can be removed because they are not needed yet.
         */
        paBatEntries = (PVhdxBatEntry)RTMemAlloc(cbBatEntries);
        if (paBatEntries)
        {
            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, offRegion,
                                       paBatEntries, cbBatEntries);
            if (RT_SUCCESS(rc))
            {
                vhdxConvBatTableEndianess(VHDXECONV_F2H, paBatEntries, paBatEntries,
                                          cBatEntries);

                /* Go through the table and validate it. */
                for (unsigned i = 0; i < cBatEntries; i++)
                {
                    if (   i != 0
                        && (i % uChunkRatio) == 0)
                    {
/**
 * Disabled the verification because there are images out there with the sector bitmap
 * marked as present. The entry is never accessed and the image is readonly anyway,
 * so no harm done.
 */
#if 0
                        /* Sector bitmap block. */
                        if (   VHDX_BAT_ENTRY_GET_STATE(paBatEntries[i].u64BatEntry)
                            != VHDX_BAT_ENTRY_SB_BLOCK_NOT_PRESENT)
                        {
                            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                                           "VHDX: Sector bitmap block at entry %u of image \'%s\' marked as present, violation of the specification",
                                           i, pImage->pszFilename);
                            break;
                        }
#endif
                    }
                    else
                    {
                        /* Payload block. */
                        if (   VHDX_BAT_ENTRY_GET_STATE(paBatEntries[i].u64BatEntry)
                            == VHDX_BAT_ENTRY_PAYLOAD_BLOCK_PARTIALLY_PRESENT)
                        {
                            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                                           "VHDX: Payload block at entry %u of image \'%s\' marked as partially present, violation of the specification",
                                           i, pImage->pszFilename);
                            break;
                        }
                    }
                }

                if (RT_SUCCESS(rc))
                {
                    pImage->paBat       = paBatEntries;
                    pImage->uChunkRatio = uChunkRatio;
                }
            }
            else
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                               "VHDX: Error reading the BAT from image \'%s\'",
                               pImage->pszFilename);
        }
        else
            rc = vdIfError(pImage->pIfError, VERR_NO_MEMORY, RT_SRC_POS,
                           "VHDX: Out of memory allocating memory for %u BAT entries of image \'%s\'",
                           cBatEntries, pImage->pszFilename);
    }
    else
        rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                       "VHDX: Mismatch between calculated number of BAT entries and region size (expected %u got %u) for image \'%s\'",
                       cbBatEntries, cbRegion, pImage->pszFilename);

    if (   RT_FAILURE(rc)
        && paBatEntries)
        RTMemFree(paBatEntries);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Load the file parameters metadata item from the file.
 *
 * @returns VBox status code.
 * @param   pImage    Image instance data.
 * @param   offItem   File offset where the data is stored.
 * @param   cbItem    Size of the item in the file.
 */
static int vhdxLoadFileParametersMetadata(PVHDXIMAGE pImage, uint64_t offItem, size_t cbItem)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p offItem=%llu cbItem=%zu\n", pImage, offItem, cbItem));

    if (cbItem != sizeof(VhdxFileParameters))
        rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                       "VHDX: File parameters item size mismatch (expected %u got %zu) in image \'%s\'",
                       sizeof(VhdxFileParameters), cbItem, pImage->pszFilename);
    else
    {
        VhdxFileParameters FileParameters;

        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, offItem,
                                   &FileParameters, sizeof(FileParameters));
        if (RT_SUCCESS(rc))
        {
            vhdxConvFileParamsEndianess(VHDXECONV_F2H, &FileParameters, &FileParameters);
            pImage->cbBlock = FileParameters.u32BlockSize;

            /** @todo No support for differencing images yet. */
            if (FileParameters.u32Flags & VHDX_FILE_PARAMETERS_FLAGS_HAS_PARENT)
                rc = vdIfError(pImage->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                               "VHDX: Image \'%s\' is a differencing image which is not supported yet",
                               pImage->pszFilename);
        }
        else
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                           "VHDX: Reading the file parameters metadata item from image \'%s\' failed",
                           pImage->pszFilename);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Load the virtual disk size metadata item from the file.
 *
 * @returns VBox status code.
 * @param   pImage    Image instance data.
 * @param   offItem   File offset where the data is stored.
 * @param   cbItem    Size of the item in the file.
 */
static int vhdxLoadVDiskSizeMetadata(PVHDXIMAGE pImage, uint64_t offItem, size_t cbItem)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p offItem=%llu cbItem=%zu\n", pImage, offItem, cbItem));

    if (cbItem != sizeof(VhdxVDiskSize))
        rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                       "VHDX: Virtual disk size item size mismatch (expected %u got %zu) in image \'%s\'",
                       sizeof(VhdxVDiskSize), cbItem, pImage->pszFilename);
    else
    {
        VhdxVDiskSize VDiskSize;

        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, offItem,
                                   &VDiskSize, sizeof(VDiskSize));
        if (RT_SUCCESS(rc))
        {
            vhdxConvVDiskSizeEndianess(VHDXECONV_F2H, &VDiskSize, &VDiskSize);
            pImage->cbSize = VDiskSize.u64VDiskSize;
        }
        else
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                           "VHDX: Reading the virtual disk size metadata item from image \'%s\' failed",
                           pImage->pszFilename);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Load the logical sector size metadata item from the file.
 *
 * @returns VBox status code.
 * @param   pImage    Image instance data.
 * @param   offItem   File offset where the data is stored.
 * @param   cbItem    Size of the item in the file.
 */
static int vhdxLoadVDiskLogSectorSizeMetadata(PVHDXIMAGE pImage, uint64_t offItem, size_t cbItem)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p offItem=%llu cbItem=%zu\n", pImage, offItem, cbItem));

    if (cbItem != sizeof(VhdxVDiskLogicalSectorSize))
        rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                       "VHDX: Virtual disk logical sector size item size mismatch (expected %u got %zu) in image \'%s\'",
                       sizeof(VhdxVDiskLogicalSectorSize), cbItem, pImage->pszFilename);
    else
    {
        VhdxVDiskLogicalSectorSize VDiskLogSectSize;

        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, offItem,
                                   &VDiskLogSectSize, sizeof(VDiskLogSectSize));
        if (RT_SUCCESS(rc))
        {
            vhdxConvVDiskLogSectSizeEndianess(VHDXECONV_F2H, &VDiskLogSectSize,
                                              &VDiskLogSectSize);
            pImage->cbLogicalSector = VDiskLogSectSize.u32LogicalSectorSize;
        }
        else
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                           "VHDX: Reading the virtual disk logical sector size metadata item from image \'%s\' failed",
                           pImage->pszFilename);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Loads the metadata region.
 *
 * @returns VBox status code.
 * @param   pImage    Image instance data.
 * @param   offRegion Start offset of the region.
 * @param   cbRegion  Size of the region.
 */
static int vhdxLoadMetadataRegion(PVHDXIMAGE pImage, uint64_t offRegion,
                                  size_t cbRegion)
{
    VhdxMetadataTblHdr MetadataTblHdr;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p\n", pImage));

    /* Load the header first. */
    rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, offRegion,
                               &MetadataTblHdr, sizeof(MetadataTblHdr));
    if (RT_SUCCESS(rc))
    {
        vhdxConvMetadataTblHdrEndianess(VHDXECONV_F2H, &MetadataTblHdr, &MetadataTblHdr);

        /* Validate structure. */
        if (MetadataTblHdr.u64Signature != VHDX_METADATA_TBL_HDR_SIGNATURE)
            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                           "VHDX: Incorrect metadata table header signature for image \'%s\'",
                           pImage->pszFilename);
        else if (MetadataTblHdr.u16EntryCount > VHDX_METADATA_TBL_HDR_ENTRY_COUNT_MAX)
            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                           "VHDX: Incorrect entry count in metadata table header of image \'%s\'",
                           pImage->pszFilename);
        else if (cbRegion < (MetadataTblHdr.u16EntryCount * sizeof(VhdxMetadataTblEntry) + sizeof(VhdxMetadataTblHdr)))
            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                           "VHDX: Metadata table of image \'%s\' exceeds region size",
                           pImage->pszFilename);

        if (RT_SUCCESS(rc))
        {
            uint64_t offMetadataTblEntry = offRegion + sizeof(VhdxMetadataTblHdr);

            for (unsigned i = 0; i < MetadataTblHdr.u16EntryCount; i++)
            {
                uint64_t offMetadataItem = 0;
                VHDXMETADATAITEM enmMetadataItem = VHDXMETADATAITEM_UNKNOWN;
                VhdxMetadataTblEntry MetadataTblEntry;

                rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, offMetadataTblEntry,
                                           &MetadataTblEntry, sizeof(MetadataTblEntry));
                if (RT_FAILURE(rc))
                {
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                   "VHDX: Reading metadata table entry from image \'%s\' failed",
                                   pImage->pszFilename);
                    break;
                }

                vhdxConvMetadataTblEntryEndianess(VHDXECONV_F2H, &MetadataTblEntry, &MetadataTblEntry);

                /* Check whether the flags match the expectations. */
                for (unsigned idxProp = 0; idxProp < RT_ELEMENTS(s_aVhdxMetadataItemProps); idxProp++)
                {
                    if (!RTUuidCompareStr(&MetadataTblEntry.UuidItem,
                                          s_aVhdxMetadataItemProps[idxProp].pszItemUuid))
                    {
                        /*
                         * Check for specification violations and bail out, except
                         * for the required flag of the physical sector size metadata item.
                         * Early images had the required flag not set opposed to the specification.
                         * We don't want to brerak those images.
                         */
                        if (   !!(MetadataTblEntry.u32Flags & VHDX_METADATA_TBL_ENTRY_FLAGS_IS_USER)
                            != s_aVhdxMetadataItemProps[idxProp].fIsUser)
                            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                                           "VHDX: User flag of metadata item does not meet expectations \'%s\'",
                                           pImage->pszFilename);
                        else if (   !!(MetadataTblEntry.u32Flags & VHDX_METADATA_TBL_ENTRY_FLAGS_IS_VDISK)
                                 != s_aVhdxMetadataItemProps[idxProp].fIsVDisk)
                            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                                           "VHDX: Virtual disk flag of metadata item does not meet expectations \'%s\'",
                                           pImage->pszFilename);
                        else if (      !!(MetadataTblEntry.u32Flags & VHDX_METADATA_TBL_ENTRY_FLAGS_IS_REQUIRED)
                                    != s_aVhdxMetadataItemProps[idxProp].fIsRequired
                                 && (s_aVhdxMetadataItemProps[idxProp].enmMetadataItem != VHDXMETADATAITEM_PHYSICAL_SECTOR_SIZE))
                            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                                           "VHDX: Required flag of metadata item does not meet expectations \'%s\'",
                                           pImage->pszFilename);
                        else
                            enmMetadataItem = s_aVhdxMetadataItemProps[idxProp].enmMetadataItem;

                        break;
                    }
                }

                if (RT_FAILURE(rc))
                    break;

                offMetadataItem = offRegion + MetadataTblEntry.u32Offset;

                switch (enmMetadataItem)
                {
                    case VHDXMETADATAITEM_FILE_PARAMS:
                    {
                        rc = vhdxLoadFileParametersMetadata(pImage, offMetadataItem,
                                                            MetadataTblEntry.u32Length);
                        break;
                    }
                    case VHDXMETADATAITEM_VDISK_SIZE:
                    {
                        rc = vhdxLoadVDiskSizeMetadata(pImage, offMetadataItem,
                                                       MetadataTblEntry.u32Length);
                        break;
                    }
                    case VHDXMETADATAITEM_PAGE83_DATA:
                    {
                        /*
                         * Nothing to do here for now (marked as required but
                         * there is no API to pass this information to the caller)
                         * so far.
                         */
                        break;
                    }
                    case VHDXMETADATAITEM_LOGICAL_SECTOR_SIZE:
                    {
                        rc = vhdxLoadVDiskLogSectorSizeMetadata(pImage, offMetadataItem,
                                                                MetadataTblEntry.u32Length);
                        break;
                    }
                    case VHDXMETADATAITEM_PHYSICAL_SECTOR_SIZE:
                    {
                        /*
                         * Nothing to do here for now (marked as required but
                         * there is no API to pass this information to the caller)
                         * so far.
                         */
                        break;
                    }
                    case VHDXMETADATAITEM_PARENT_LOCATOR:
                    {
                        rc = vdIfError(pImage->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                                       "VHDX: Image \'%s\' is a differencing image which is not supported yet",
                                       pImage->pszFilename);
                        break;
                    }
                    case VHDXMETADATAITEM_UNKNOWN:
                    default:
                        if (MetadataTblEntry.u32Flags & VHDX_METADATA_TBL_ENTRY_FLAGS_IS_REQUIRED)
                            rc = vdIfError(pImage->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                                           "VHDX: Unsupported but required metadata item in image \'%s\'",
                                           pImage->pszFilename);
                }

                if (RT_FAILURE(rc))
                    break;

                offMetadataTblEntry += sizeof(MetadataTblEntry);
            }
        }
    }
    else
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                       "VHDX: Reading the metadata table header for image \'%s\' failed",
                       pImage->pszFilename);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Loads the region table and the associated regions.
 *
 * @returns VBox status code.
 * @param   pImage    Image instance data.
 */
static int vhdxLoadRegionTable(PVHDXIMAGE pImage)
{
    uint8_t *pbRegionTbl = NULL;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p\n", pImage));

    /* Load the complete region table into memory. */
    pbRegionTbl = (uint8_t *)RTMemTmpAlloc(VHDX_REGION_TBL_SIZE_MAX);
    if (pbRegionTbl)
    {
        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, VHDX_REGION_TBL_HDR_OFFSET,
                                   pbRegionTbl, VHDX_REGION_TBL_SIZE_MAX);
        if (RT_SUCCESS(rc))
        {
            PVhdxRegionTblHdr pRegionTblHdr;
            VhdxRegionTblHdr RegionTblHdr;
            uint32_t u32ChkSum = 0;

            /*
             * Copy the region table header to a dedicated structure where we can
             * convert it to host endianess.
             */
            memcpy(&RegionTblHdr, pbRegionTbl, sizeof(RegionTblHdr));
            vhdxConvRegionTblHdrEndianess(VHDXECONV_F2H, &RegionTblHdr, &RegionTblHdr);

            /* Set checksum field to 0 during crc computation. */
            pRegionTblHdr = (PVhdxRegionTblHdr)pbRegionTbl;
            pRegionTblHdr->u32Checksum = 0;

            /* Verify the region table integrity. */
            u32ChkSum = RTCrc32C(pbRegionTbl, VHDX_REGION_TBL_SIZE_MAX);

            if (RegionTblHdr.u32Signature != VHDX_REGION_TBL_HDR_SIGNATURE)
                rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                               "VHDX: Invalid signature for region table header of image \'%s\'",
                               pImage->pszFilename);
            else if (u32ChkSum != RegionTblHdr.u32Checksum)
                rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                               "VHDX: CRC32 checksum mismatch for the region table of image \'%s\' (expected %#x got %#x)",
                               pImage->pszFilename, RegionTblHdr.u32Checksum, u32ChkSum);
            else if (RegionTblHdr.u32EntryCount > VHDX_REGION_TBL_HDR_ENTRY_COUNT_MAX)
                rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                               "VHDX: Invalid entry count field in the region table header of image \'%s\'",
                               pImage->pszFilename);

            if (RT_SUCCESS(rc))
            {
                /* Parse the region table entries. */
                PVhdxRegionTblEntry pRegTblEntry = (PVhdxRegionTblEntry)(pbRegionTbl + sizeof(VhdxRegionTblHdr));
                VhdxRegionTblEntry RegTblEntryBat; /* BAT region table entry. */
                bool fBatRegPresent = false;
                RT_ZERO(RegTblEntryBat); /* Maybe uninitialized, gcc. */

                for (unsigned i = 0; i < RegionTblHdr.u32EntryCount; i++)
                {
                    vhdxConvRegionTblEntryEndianess(VHDXECONV_F2H, pRegTblEntry, pRegTblEntry);

                    /* Check the uuid for known regions. */
                    if (!RTUuidCompareStr(&pRegTblEntry->UuidObject, VHDX_REGION_TBL_ENTRY_UUID_BAT))
                    {
                        /*
                         * Save the BAT region and process it later.
                         * It may come before the metadata region but needs the block size.
                         */
                        if (pRegTblEntry->u32Flags & VHDX_REGION_TBL_ENTRY_FLAGS_IS_REQUIRED)
                        {
                            fBatRegPresent = true;
                            RegTblEntryBat.u32Length = pRegTblEntry->u32Length;
                            RegTblEntryBat.u64FileOffset = pRegTblEntry->u64FileOffset;
                        }
                        else
                            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                                           "VHDX: BAT region not marked as required in image \'%s\'",
                                           pImage->pszFilename);
                    }
                    else if (!RTUuidCompareStr(&pRegTblEntry->UuidObject, VHDX_REGION_TBL_ENTRY_UUID_METADATA))
                    {
                        if (pRegTblEntry->u32Flags & VHDX_REGION_TBL_ENTRY_FLAGS_IS_REQUIRED)
                            rc = vhdxLoadMetadataRegion(pImage, pRegTblEntry->u64FileOffset, pRegTblEntry->u32Length);
                        else
                            rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                                           "VHDX: Metadata region not marked as required in image \'%s\'",
                                           pImage->pszFilename);
                    }
                    else if (pRegTblEntry->u32Flags & VHDX_REGION_TBL_ENTRY_FLAGS_IS_REQUIRED)
                    {
                        /* The region is not known but marked as required, fail to load the image. */
                        rc = vdIfError(pImage->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                                       "VHDX: Unknown required region in image \'%s\'",
                                       pImage->pszFilename);
                    }

                    if (RT_FAILURE(rc))
                        break;

                    pRegTblEntry++;
                }

                if (fBatRegPresent)
                    rc = vhdxLoadBatRegion(pImage, RegTblEntryBat.u64FileOffset, RegTblEntryBat.u32Length);
                else
                    rc = vdIfError(pImage->pIfError, VERR_VD_GEN_INVALID_HEADER, RT_SRC_POS,
                                   "VHDX: BAT region in image \'%s\' is missing",
                                   pImage->pszFilename);
            }
        }
        else
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                           "VHDX: Reading the region table for image \'%s\' failed",
                           pImage->pszFilename);
    }
    else
        rc = vdIfError(pImage->pIfError, VERR_NO_MEMORY, RT_SRC_POS,
                       "VHDX: Out of memory allocating memory for the region table of image \'%s\'",
                       pImage->pszFilename);

    if (pbRegionTbl)
        RTMemTmpFree(pbRegionTbl);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Internal: Open an image, constructing all necessary data structures.
 */
static int vhdxOpenImage(PVHDXIMAGE pImage, unsigned uOpenFlags)
{
    uint64_t cbFile = 0;
    VhdxFileIdentifier FileIdentifier;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p uOpenFlags=%#x\n", pImage, uOpenFlags));
    pImage->uOpenFlags = uOpenFlags;

    pImage->pIfError = VDIfErrorGet(pImage->pVDIfsDisk);
    pImage->pIfIo = VDIfIoIntGet(pImage->pVDIfsImage);
    AssertPtrReturn(pImage->pIfIo, VERR_INVALID_PARAMETER);

    /* Refuse write access, it is not implemented so far. */
    if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
        return VERR_NOT_SUPPORTED;

    /*
     * Open the image.
     */
    rc = vdIfIoIntFileOpen(pImage->pIfIo, pImage->pszFilename,
                           VDOpenFlagsToFileOpenFlags(uOpenFlags,
                                                      false /* fCreate */),
                           &pImage->pStorage);

    /* Do NOT signal an appropriate error here, as the VD layer has the
     * choice of retrying the open if it failed. */
    if (RT_SUCCESS(rc))
        rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pStorage, &cbFile);

    if (RT_SUCCESS(rc))
    {
        if (cbFile > sizeof(FileIdentifier))
        {
            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, VHDX_FILE_IDENTIFIER_OFFSET,
                                       &FileIdentifier, sizeof(FileIdentifier));
            if (RT_SUCCESS(rc))
            {
                vhdxConvFileIdentifierEndianess(VHDXECONV_F2H, &FileIdentifier,
                                                &FileIdentifier);
                if (FileIdentifier.u64Signature != VHDX_FILE_IDENTIFIER_SIGNATURE)
                    rc = VERR_VD_GEN_INVALID_HEADER;
                else
                    rc = vhdxFindAndLoadCurrentHeader(pImage);

                /* Load the region table. */
                if (RT_SUCCESS(rc))
                    rc = vhdxLoadRegionTable(pImage);
            }
        }
        else
            rc = VERR_VD_GEN_INVALID_HEADER;
    }

    if (RT_SUCCESS(rc))
    {
        PVDREGIONDESC pRegion = &pImage->RegionList.aRegions[0];
        pImage->RegionList.fFlags   = 0;
        pImage->RegionList.cRegions = 1;

        pRegion->offRegion            = 0; /* Disk start. */
        pRegion->cbBlock              = pImage->cbLogicalSector;
        pRegion->enmDataForm          = VDREGIONDATAFORM_RAW;
        pRegion->enmMetadataForm      = VDREGIONMETADATAFORM_NONE;
        pRegion->cbData               = pImage->cbLogicalSector;
        pRegion->cbMetadata           = 0;
        pRegion->cRegionBlocksOrBytes = pImage->cbSize;
    }
    else
        vhdxFreeImage(pImage, false);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}


/** @copydoc VDIMAGEBACKEND::pfnProbe */
static DECLCALLBACK(int) vhdxProbe(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                                   PVDINTERFACE pVDIfsImage, VDTYPE enmDesiredType, VDTYPE *penmType)
{
    RT_NOREF(pVDIfsDisk, enmDesiredType);
    LogFlowFunc(("pszFilename=\"%s\" pVDIfsDisk=%#p pVDIfsImage=%#p\n", pszFilename, pVDIfsDisk, pVDIfsImage));
    PVDIOSTORAGE pStorage = NULL;
    uint64_t cbFile;
    int rc = VINF_SUCCESS;
    VhdxFileIdentifier FileIdentifier;

    PVDINTERFACEIOINT pIfIo = VDIfIoIntGet(pVDIfsImage);
    AssertPtrReturn(pIfIo, VERR_INVALID_PARAMETER);

    if (   !RT_VALID_PTR(pszFilename)
        || !*pszFilename)
        rc = VERR_INVALID_PARAMETER;
    else
    {
        /*
         * Open the file and read the file identifier.
         */
        rc = vdIfIoIntFileOpen(pIfIo, pszFilename,
                               VDOpenFlagsToFileOpenFlags(VD_OPEN_FLAGS_READONLY,
                                                          false /* fCreate */),
                               &pStorage);
        if (RT_SUCCESS(rc))
        {
            rc = vdIfIoIntFileGetSize(pIfIo, pStorage, &cbFile);
            if (RT_SUCCESS(rc))
            {
                if (cbFile > sizeof(FileIdentifier))
                {
                    rc = vdIfIoIntFileReadSync(pIfIo, pStorage, VHDX_FILE_IDENTIFIER_OFFSET,
                                               &FileIdentifier, sizeof(FileIdentifier));
                    if (RT_SUCCESS(rc))
                    {
                        vhdxConvFileIdentifierEndianess(VHDXECONV_F2H, &FileIdentifier,
                                                        &FileIdentifier);
                        if (FileIdentifier.u64Signature != VHDX_FILE_IDENTIFIER_SIGNATURE)
                            rc = VERR_VD_GEN_INVALID_HEADER;
                        else
                            *penmType = VDTYPE_HDD;
                    }
                }
                else
                    rc = VERR_VD_GEN_INVALID_HEADER;
            }
        }

        if (pStorage)
            vdIfIoIntFileClose(pIfIo, pStorage);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnOpen */
static DECLCALLBACK(int) vhdxOpen(const char *pszFilename, unsigned uOpenFlags,
                                  PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                  VDTYPE enmType, void **ppBackendData)
{
    LogFlowFunc(("pszFilename=\"%s\" uOpenFlags=%#x pVDIfsDisk=%#p pVDIfsImage=%#p enmType=%u ppBackendData=%#p\n", pszFilename, uOpenFlags, pVDIfsDisk, pVDIfsImage, enmType, ppBackendData));
    int rc;
    PVHDXIMAGE pImage;

    NOREF(enmType); /**< @todo r=klaus make use of the type info. */

    /* Check open flags. All valid flags are supported. */
    if (   uOpenFlags & ~VD_OPEN_FLAGS_MASK
        || !RT_VALID_PTR(pszFilename)
        || !*pszFilename)
        rc = VERR_INVALID_PARAMETER;
    else
    {
        pImage = (PVHDXIMAGE)RTMemAllocZ(RT_UOFFSETOF(VHDXIMAGE, RegionList.aRegions[1]));
        if (!pImage)
            rc = VERR_NO_MEMORY;
        else
        {
            pImage->pszFilename = pszFilename;
            pImage->pStorage = NULL;
            pImage->pVDIfsDisk = pVDIfsDisk;
            pImage->pVDIfsImage = pVDIfsImage;

            rc = vhdxOpenImage(pImage, uOpenFlags);
            if (RT_SUCCESS(rc))
                *ppBackendData = pImage;
            else
                RTMemFree(pImage);
        }
    }

    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @interface_method_impl{VDIMAGEBACKEND,pfnCreate} */
static DECLCALLBACK(int) vhdxCreate(const char *pszFilename, uint64_t cbSize,
                                    unsigned uImageFlags, const char *pszComment,
                                    PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                                    PCRTUUID pUuid, unsigned uOpenFlags,
                                    unsigned uPercentStart, unsigned uPercentSpan,
                                    PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                    PVDINTERFACE pVDIfsOperation, VDTYPE enmType,
                                    void **ppBackendData)
{
    RT_NOREF8(pszFilename, cbSize, uImageFlags, pszComment, pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags);
    RT_NOREF7(uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, enmType, ppBackendData);
    LogFlowFunc(("pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" pPCHSGeometry=%#p pLCHSGeometry=%#p Uuid=%RTuuid uOpenFlags=%#x uPercentStart=%u uPercentSpan=%u pVDIfsDisk=%#p pVDIfsImage=%#p pVDIfsOperation=%#p enmType=%u ppBackendData=%#p",
                 pszFilename, cbSize, uImageFlags, pszComment, pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags, uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, enmType, ppBackendData));
    int rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRename */
static DECLCALLBACK(int) vhdxRename(void *pBackendData, const char *pszFilename)
{
    LogFlowFunc(("pBackendData=%#p pszFilename=%#p\n", pBackendData, pszFilename));
    int rc = VINF_SUCCESS;
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;

    /* Check arguments. */
    if (   !pImage
        || !pszFilename
        || !*pszFilename)
        rc = VERR_INVALID_PARAMETER;
    else
    {
        /* Close the image. */
        rc = vhdxFreeImage(pImage, false);
        if (RT_SUCCESS(rc))
        {
            /* Rename the file. */
            rc = vdIfIoIntFileMove(pImage->pIfIo, pImage->pszFilename, pszFilename, 0);
            if (RT_FAILURE(rc))
            {
                /* The move failed, try to reopen the original image. */
                int rc2 = vhdxOpenImage(pImage, pImage->uOpenFlags);
                if (RT_FAILURE(rc2))
                    rc = rc2;
            }
            else
            {
                /* Update pImage with the new information. */
                pImage->pszFilename = pszFilename;

                /* Open the old image with new name. */
                rc = vhdxOpenImage(pImage, pImage->uOpenFlags);
            }
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnClose */
static DECLCALLBACK(int) vhdxClose(void *pBackendData, bool fDelete)
{
    LogFlowFunc(("pBackendData=%#p fDelete=%d\n", pBackendData, fDelete));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    int rc;

    rc = vhdxFreeImage(pImage, fDelete);
    RTMemFree(pImage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRead */
static DECLCALLBACK(int) vhdxRead(void *pBackendData, uint64_t uOffset, size_t cbToRead,
                                  PVDIOCTX pIoCtx, size_t *pcbActuallyRead)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToRead=%zu pcbActuallyRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToRead, pcbActuallyRead));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);
    Assert(uOffset % 512 == 0);
    Assert(cbToRead % 512 == 0);

    if (   uOffset + cbToRead > pImage->cbSize
        || cbToRead == 0)
        rc = VERR_INVALID_PARAMETER;
    else
    {
        uint32_t idxBat = (uint32_t)(uOffset / pImage->cbBlock); Assert(idxBat == uOffset / pImage->cbBlock);
        uint32_t offRead = uOffset % pImage->cbBlock;
        uint64_t uBatEntry;

        idxBat += idxBat / pImage->uChunkRatio; /* Add interleaving sector bitmap entries. */
        uBatEntry = pImage->paBat[idxBat].u64BatEntry;

        cbToRead = RT_MIN(cbToRead, pImage->cbBlock - offRead);

        switch (VHDX_BAT_ENTRY_GET_STATE(uBatEntry))
        {
            case VHDX_BAT_ENTRY_PAYLOAD_BLOCK_NOT_PRESENT:
            case VHDX_BAT_ENTRY_PAYLOAD_BLOCK_UNDEFINED:
            case VHDX_BAT_ENTRY_PAYLOAD_BLOCK_ZERO:
            case VHDX_BAT_ENTRY_PAYLOAD_BLOCK_UNMAPPED:
            {
                vdIfIoIntIoCtxSet(pImage->pIfIo, pIoCtx, 0, cbToRead);
                break;
            }
            case VHDX_BAT_ENTRY_PAYLOAD_BLOCK_FULLY_PRESENT:
            {
                uint64_t offFile = VHDX_BAT_ENTRY_GET_FILE_OFFSET(uBatEntry) + offRead;
                rc = vdIfIoIntFileReadUser(pImage->pIfIo, pImage->pStorage, offFile,
                                           pIoCtx, cbToRead);
                break;
            }
            case VHDX_BAT_ENTRY_PAYLOAD_BLOCK_PARTIALLY_PRESENT:
            default:
                rc = VERR_INVALID_PARAMETER;
                break;
        }

        if (pcbActuallyRead)
            *pcbActuallyRead = cbToRead;
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnWrite */
static DECLCALLBACK(int) vhdxWrite(void *pBackendData, uint64_t uOffset,  size_t cbToWrite,
                                   PVDIOCTX pIoCtx, size_t *pcbWriteProcess, size_t *pcbPreRead,
                                   size_t *pcbPostRead, unsigned fWrite)
{
    RT_NOREF5(pIoCtx, pcbWriteProcess, pcbPreRead, pcbPostRead, fWrite);
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToWrite=%zu pcbWriteProcess=%#p pcbPreRead=%#p pcbPostRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToWrite, pcbWriteProcess, pcbPreRead, pcbPostRead));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);
    Assert(uOffset % 512 == 0);
    Assert(cbToWrite % 512 == 0);

    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else if (   uOffset + cbToWrite > pImage->cbSize
             || cbToWrite == 0)
        rc = VERR_INVALID_PARAMETER;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnFlush */
static DECLCALLBACK(int) vhdxFlush(void *pBackendData, PVDIOCTX pIoCtx)
{
    RT_NOREF1(pIoCtx);
    LogFlowFunc(("pBackendData=%#p pIoCtx=%#p\n", pBackendData, pIoCtx));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    int rc;

    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetVersion */
static DECLCALLBACK(unsigned) vhdxGetVersion(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;

    AssertPtr(pImage);

    if (pImage)
        return pImage->uVersion;
    else
        return 0;
}

/** @copydoc VDIMAGEBACKEND::pfnGetFileSize */
static DECLCALLBACK(uint64_t) vhdxGetFileSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    uint64_t cb = 0;

    AssertPtr(pImage);

    if (pImage)
    {
        uint64_t cbFile;
        if (pImage->pStorage)
        {
            int rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pStorage, &cbFile);
            if (RT_SUCCESS(rc))
                cb = cbFile;
        }
    }

    LogFlowFunc(("returns %lld\n", cb));
    return cb;
}

/** @copydoc VDIMAGEBACKEND::pfnGetPCHSGeometry */
static DECLCALLBACK(int) vhdxGetPCHSGeometry(void *pBackendData,
                                             PVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p\n", pBackendData, pPCHSGeometry));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->PCHSGeometry.cCylinders)
        {
            *pPCHSGeometry = pImage->PCHSGeometry;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_VD_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (PCHS=%u/%u/%u)\n", rc, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetPCHSGeometry */
static DECLCALLBACK(int) vhdxSetPCHSGeometry(void *pBackendData,
                                             PCVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p PCHS=%u/%u/%u\n", pBackendData, pPCHSGeometry, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            rc = VERR_VD_IMAGE_READ_ONLY;
        else
            pImage->PCHSGeometry = *pPCHSGeometry;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetLCHSGeometry */
static DECLCALLBACK(int) vhdxGetLCHSGeometry(void *pBackendData,
                                             PVDGEOMETRY pLCHSGeometry)
{
     LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p\n", pBackendData, pLCHSGeometry));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->LCHSGeometry.cCylinders)
            *pLCHSGeometry = pImage->LCHSGeometry;
        else
            rc = VERR_VD_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (LCHS=%u/%u/%u)\n", rc, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetLCHSGeometry */
static DECLCALLBACK(int) vhdxSetLCHSGeometry(void *pBackendData,
                                             PCVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p LCHS=%u/%u/%u\n", pBackendData, pLCHSGeometry, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            rc = VERR_VD_IMAGE_READ_ONLY;
        else
            pImage->LCHSGeometry = *pLCHSGeometry;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnQueryRegions */
static DECLCALLBACK(int) vhdxQueryRegions(void *pBackendData, PCVDREGIONLIST *ppRegionList)
{
    LogFlowFunc(("pBackendData=%#p ppRegionList=%#p\n", pBackendData, ppRegionList));
    PVHDXIMAGE pThis = (PVHDXIMAGE)pBackendData;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    *ppRegionList = &pThis->RegionList;
    LogFlowFunc(("returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnRegionListRelease */
static DECLCALLBACK(void) vhdxRegionListRelease(void *pBackendData, PCVDREGIONLIST pRegionList)
{
    RT_NOREF1(pRegionList);
    LogFlowFunc(("pBackendData=%#p pRegionList=%#p\n", pBackendData, pRegionList));
    PVHDXIMAGE pThis = (PVHDXIMAGE)pBackendData;
    AssertPtr(pThis); RT_NOREF(pThis);

    /* Nothing to do here. */
}

/** @copydoc VDIMAGEBACKEND::pfnGetImageFlags */
static DECLCALLBACK(unsigned) vhdxGetImageFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    unsigned uImageFlags;

    AssertPtr(pImage);

    if (pImage)
        uImageFlags = pImage->uImageFlags;
    else
        uImageFlags = 0;

    LogFlowFunc(("returns %#x\n", uImageFlags));
    return uImageFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnGetOpenFlags */
static DECLCALLBACK(unsigned) vhdxGetOpenFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    unsigned uOpenFlags;

    AssertPtr(pImage);

    if (pImage)
        uOpenFlags = pImage->uOpenFlags;
    else
        uOpenFlags = 0;

    LogFlowFunc(("returns %#x\n", uOpenFlags));
    return uOpenFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnSetOpenFlags */
static DECLCALLBACK(int) vhdxSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    LogFlowFunc(("pBackendData=%#p\n uOpenFlags=%#x", pBackendData, uOpenFlags));
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    /* Image must be opened and the new flags must be valid. */
    if (!pImage || (uOpenFlags & ~(VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO | VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS)))
        rc = VERR_INVALID_PARAMETER;
    else
    {
        /* Implement this operation via reopening the image. */
        rc = vhdxFreeImage(pImage, false);
        if (RT_SUCCESS(rc))
            rc = vhdxOpenImage(pImage, uOpenFlags);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetComment */
VD_BACKEND_CALLBACK_GET_COMMENT_DEF_NOT_SUPPORTED(vhdxGetComment);

/** @copydoc VDIMAGEBACKEND::pfnSetComment */
VD_BACKEND_CALLBACK_SET_COMMENT_DEF_NOT_SUPPORTED(vhdxSetComment, PVHDXIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(vhdxGetUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(vhdxSetUuid, PVHDXIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetModificationUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(vhdxGetModificationUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetModificationUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(vhdxSetModificationUuid, PVHDXIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetParentUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(vhdxGetParentUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetParentUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(vhdxSetParentUuid, PVHDXIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetParentModificationUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(vhdxGetParentModificationUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetParentModificationUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(vhdxSetParentModificationUuid, PVHDXIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnDump */
static DECLCALLBACK(void) vhdxDump(void *pBackendData)
{
    PVHDXIMAGE pImage = (PVHDXIMAGE)pBackendData;

    AssertPtr(pImage);
    if (pImage)
    {
        vdIfErrorMessage(pImage->pIfError, "Header: Geometry PCHS=%u/%u/%u LCHS=%u/%u/%u cbSector=%u\n",
                        pImage->PCHSGeometry.cCylinders, pImage->PCHSGeometry.cHeads, pImage->PCHSGeometry.cSectors,
                        pImage->LCHSGeometry.cCylinders, pImage->LCHSGeometry.cHeads, pImage->LCHSGeometry.cSectors,
                        pImage->cbLogicalSector);
    }
}


const VDIMAGEBACKEND g_VhdxBackend =
{
    /* u32Version */
    VD_IMGBACKEND_VERSION,
    /* pszBackendName */
    "VHDX",
    /* uBackendCaps */
    VD_CAP_FILE | VD_CAP_VFS,
    /* paFileExtensions */
    s_aVhdxFileExtensions,
    /* paConfigInfo */
    NULL,
    /* pfnProbe */
    vhdxProbe,
    /* pfnOpen */
    vhdxOpen,
    /* pfnCreate */
    vhdxCreate,
    /* pfnRename */
    vhdxRename,
    /* pfnClose */
    vhdxClose,
    /* pfnRead */
    vhdxRead,
    /* pfnWrite */
    vhdxWrite,
    /* pfnFlush */
    vhdxFlush,
    /* pfnDiscard */
    NULL,
    /* pfnGetVersion */
    vhdxGetVersion,
    /* pfnGetFileSize */
    vhdxGetFileSize,
    /* pfnGetPCHSGeometry */
    vhdxGetPCHSGeometry,
    /* pfnSetPCHSGeometry */
    vhdxSetPCHSGeometry,
    /* pfnGetLCHSGeometry */
    vhdxGetLCHSGeometry,
    /* pfnSetLCHSGeometry */
    vhdxSetLCHSGeometry,
    /* pfnQueryRegions */
    vhdxQueryRegions,
    /* pfnRegionListRelease */
    vhdxRegionListRelease,
    /* pfnGetImageFlags */
    vhdxGetImageFlags,
    /* pfnGetOpenFlags */
    vhdxGetOpenFlags,
    /* pfnSetOpenFlags */
    vhdxSetOpenFlags,
    /* pfnGetComment */
    vhdxGetComment,
    /* pfnSetComment */
    vhdxSetComment,
    /* pfnGetUuid */
    vhdxGetUuid,
    /* pfnSetUuid */
    vhdxSetUuid,
    /* pfnGetModificationUuid */
    vhdxGetModificationUuid,
    /* pfnSetModificationUuid */
    vhdxSetModificationUuid,
    /* pfnGetParentUuid */
    vhdxGetParentUuid,
    /* pfnSetParentUuid */
    vhdxSetParentUuid,
    /* pfnGetParentModificationUuid */
    vhdxGetParentModificationUuid,
    /* pfnSetParentModificationUuid */
    vhdxSetParentModificationUuid,
    /* pfnDump */
    vhdxDump,
    /* pfnGetTimestamp */
    NULL,
    /* pfnGetParentTimestamp */
    NULL,
    /* pfnSetParentTimestamp */
    NULL,
    /* pfnGetParentFilename */
    NULL,
    /* pfnSetParentFilename */
    NULL,
    /* pfnComposeLocation */
    genericFileComposeLocation,
    /* pfnComposeName */
    genericFileComposeName,
    /* pfnCompact */
    NULL,
    /* pfnResize */
    NULL,
    /* pfnRepair */
    NULL,
    /* pfnTraverseMetadata */
    NULL,
    /* u32VersionEnd */
    VD_IMGBACKEND_VERSION
};
