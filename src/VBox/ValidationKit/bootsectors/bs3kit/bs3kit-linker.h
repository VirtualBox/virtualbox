/* $Id: bs3kit-linker.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Linker related structures and defines.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef BS3KIT_INCLUDED_bs3kit_linker_h
#define BS3KIT_INCLUDED_bs3kit_linker_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#pragma pack(1)
typedef struct BS3BOOTSECTOR
{
    uint8_t     abJmp[3];
    char        abOemId[8];
    /** @name EBPB, DOS 4.0 style.
     * @{  */
    uint16_t    cBytesPerSector;        /**< 00bh */
    uint8_t     cSectorsPerCluster;     /**< 00dh */
    uint16_t    cReservedSectors;       /**< 00eh */
    uint8_t     cFATs;                  /**< 010h */
    uint16_t    cRootDirEntries;        /**< 011h */
    uint16_t    cTotalSectors;          /**< 013h - We (ab)use this for the checksum length in sectors. */
    uint8_t     bMediaDescriptor;       /**< 015h */
    uint16_t    cSectorsPerFAT;         /**< 016h */
    uint16_t    cPhysSectorsPerTrack;   /**< 018h */
    uint16_t    cHeads;                 /**< 01ah */
    uint32_t    cHiddentSectors;        /**< 01ch */
    uint32_t    cLargeTotalSectors;     /**< 020h - We (ab)use this to indicate the number of sectors to load. */
    uint8_t     bBootDrv;               /**< 024h */
    uint8_t     bFlagsEtc;              /**< 025h */
    uint8_t     bExtendedSignature;     /**< 026h */
    uint32_t    dwSerialNumber;         /**< 027h - We (ab)use this for the base image checksum. */
    char        abLabel[11];            /**< 02bh */
    char        abFSType[8];            /**< 036h */
    /** @} */
} BS3BOOTSECTOR;
#pragma pack()
typedef BS3BOOTSECTOR *PBS3BOOTSECTOR;

AssertCompileMemberOffset(BS3BOOTSECTOR, cLargeTotalSectors, 0x20);
AssertCompileMemberOffset(BS3BOOTSECTOR, abLabel, 0x2b);
AssertCompileMemberOffset(BS3BOOTSECTOR, abFSType, 0x36);

#define BS3_OEMID                       "BS3Kit\n\n"
#define BS3_FSTYPE                      "RawCode\n"
#define BS3_LABEL                       "VirtualBox\n"
#define BS3_MAX_SIZE                    UINT32_C(491520) /* 480KB */


/** The default address to start load high DLLs at.
 * We start at 8MB to make sure the start is 16-bit tiled addressable, that we
 * can load high DLLs on 286es, and to avoid using up the upper/tiled memory. */
#define BS3HIGHDLL_LOAD_ADDRESS         ( _8M )

/**
 * High DLL table entry.
 * These are found at the end of the base image.
 */
typedef struct BS3HIGHDLLENTRY
{
    char        achMagic[8];        /**< BS3HIGHDLLENTRY_MAGIC */
    uint32_t    uLoadAddr;          /**< The load address. Set by the linker. */
    uint32_t    cbLoaded;           /**< The number of bytes when loaded. */
    uint32_t    offInImage;         /**< Byte offset in the linked image blob. */
    uint32_t    cbInImage;          /**< The number of bytes to load from the linked image blob. */
    uint32_t    cImports;           /**< Number of imports. */
    int32_t     offImports;         /**< Relative address (to entry start) of the import table. */
    uint32_t    cExports;           /**< Number of exports. */
    int32_t     offExports;         /**< Relative address (to entry start) of the export table. */
    uint32_t    cSegments;          /**< Number of segments. */
    int32_t     offSegments;        /**< Relative address (to entry start) of the segment table. */
    uint32_t    cbStrings;          /**< Size of the string table in bytes. */
    int32_t     offStrings;         /**< Relative address (to entry start) of the string table. */
    uint32_t    offFilename;        /**< String table offset of the DLL name (sans path, with extension). */
    uint32_t    uChecksum;          /**< Simple checksum of all the on-disk image bits. */
} BS3HIGHDLLENTRY;
/** Magic value for BS3HIGHDLLENTRY. */
#define BS3HIGHDLLENTRY_MAGIC           "HighDLL"

typedef struct BS3HIGHDLLIMPORTENTRY
{
    uint16_t    offName;
    uint16_t    uSeg;
    uint32_t    offSeg;             /**< We'll see... */
    uint32_t    offFlat;
} BS3HIGHDLLIMPORTENTRY;

typedef struct BS3HIGHDLLEXPORTENTRY
{
    uint32_t    offFlat;            /**< The flat address. This can be accessed as 32-bit and 64-bit - g_pfnSymbol. */
    uint32_t    offFlatZeroHigh;    /**< Zero high dword in case of 64-bit use. */
    uint32_t    offSeg;             /**< Segmented address: offset part - g_fpfn48Symbol. */
    uint16_t    idxSel;             /**< Segmented address: selector part. */
    uint16_t    offName;            /**< The string table offset. */
} BS3HIGHDLLEXPORTENTRY;

typedef struct BS3HIGHDLLSEGMENT
{
    uint32_t    uAddr;
    uint32_t    cb;
    uint16_t    idxSel;
    uint16_t    fFlags;
} BS3HIGHDLLSEGMENT;
typedef BS3HIGHDLLSEGMENT RT_FAR *PBS3HIGHDLLSEGMENT;
/** @name BS3HIGHDLLSEGMENT_F_XXX - segment flags.
 * @{ */
#define BS3HIGHDLLSEGMENT_F_EXEC         0x0001
#define BS3HIGHDLLSEGMENT_F_16BIT        0x0002
#define BS3HIGHDLLSEGMENT_F_32BIT        0x0004
#define BS3HIGHDLLSEGMENT_F_64BIT        0x0008
#define BS3HIGHDLLSEGMENT_F_CONFORMING   0x0010
/** @} */


/** @name High DLL selector ranges.
 * @{ */
#define BS3_SEL_HIGH16_CS_FIRST     0x2908
#define BS3_SEL_HIGH16_CS_COUNT     0x18
#define BS3_SEL_HIGH16_DS_FIRST     0x29c8
#define BS3_SEL_HIGH16_DS_COUNT     0x08
#define BS3_SEL_HIGH32_CS           0x0118
#define BS3_SEL_HIGH32_DS           0x0120
#define BS3_SEL_HIGH64_CS           0x0130
#define BS3_SEL_HIGH64_DS           0x0138
#ifdef BS3KIT_INCLUDED_bs3kit_h
AssertCompile(BS3_SEL_HIGH16_CS_00 == BS3_SEL_HIGH16_CS_FIRST);
AssertCompile(BS3_SEL_HIGH16_DS_00 == BS3_SEL_HIGH16_DS_FIRST);
AssertCompile(BS3_SEL_HIGH32_CS    == BS3_SEL_R0_CS32);
AssertCompile(BS3_SEL_HIGH32_DS    == BS3_SEL_R0_DS32);
AssertCompile(BS3_SEL_HIGH64_CS    == BS3_SEL_R0_CS64);
AssertCompile(BS3_SEL_HIGH64_DS    == BS3_SEL_R0_DS64);
#endif
/** @} */


/** Initial value for Bs3CalcChecksum. */
#define BS3_CALC_CHECKSUM_INITIAL_VALUE 1

/**
 * Calculates an Adler-32 checksum.
 */
DECLINLINE(uint32_t) Bs3CalcChecksum(uint32_t uChecksum, uint8_t const RT_FAR *pbSrc, size_t cbSrc)
{
    uint32_t uA = RT_LO_U16(uChecksum);
    uint32_t uB = RT_HI_U16(uChecksum);

    while (cbSrc-- > 0)
    {
        uA = (uA + *pbSrc++) % 0xfff1;
        uB = (uA + uB)       % 0xfff1;
    }

    return RT_MAKE_U32(uA, uB);
}

#endif /* !BS3KIT_INCLUDED_bs3kit_linker_h */

