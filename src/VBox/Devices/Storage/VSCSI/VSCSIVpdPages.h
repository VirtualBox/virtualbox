/* $Id: VSCSIVpdPages.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual SCSI driver: Definitions for VPD pages.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Storage_VSCSI_VSCSIVpdPages_h
#define VBOX_INCLUDED_SRC_Storage_VSCSI_VSCSIVpdPages_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/stdint.h>

/** VPD device identification page number. */
#define VSCSI_VPD_DEVID_NUMBER 0x83
/** VPD device identification size. */
#define VSCSI_VPD_DEVID_SIZE   4
/**
 * Device identification VPD page data.
 */
#pragma pack(1)
typedef struct VSCSIVPDPAGEDEVID
{
    /** Device type. */
    unsigned u5PeripheralDeviceType : 5;    /**< 0x00 / 00 */
    /** Qualifier. */
    unsigned u3PeripheralQualifier  : 3;
    /** Page number. */
    unsigned u8PageCode             : 8;
    /** Page size (Big endian) */
    unsigned u16PageLength          : 16;
} VSCSIVPDPAGEDEVID;
#pragma pack()
AssertCompileSize(VSCSIVPDPAGEDEVID, VSCSI_VPD_DEVID_SIZE);
typedef VSCSIVPDPAGEDEVID *PVSCSIVPDPAGEDEVID;
typedef const VSCSIVPDPAGEDEVID *PCVSCSIVPDPAGEDEVID;

/** VPD supported VPD pages page number. */
#define VSCSI_VPD_SUPPORTED_PAGES_NUMBER 0x00
/** VPD supported VPD pages size. */
#define VSCSI_VPD_SUPPORTED_PAGES_SIZE   4
/**
 * Block limits VPD page data.
 */
#pragma pack(1)
typedef struct VSCSIVPDPAGESUPPORTEDPAGES
{
    /** Device type. */
    unsigned u5PeripheralDeviceType : 5;    /**< 0x00 / 00 */
    /** Qualifier. */
    unsigned u3PeripheralQualifier  : 3;
    /** Page number. */
    unsigned u8PageCode             : 8;
    /** Page size (Big endian) */
    unsigned u16PageLength          : 16;
    /** Supported pages array - variable. */
    uint8_t  abVpdPages[1];
} VSCSIVPDPAGESUPPORTEDPAGES;
#pragma pack()
AssertCompileSize(VSCSIVPDPAGESUPPORTEDPAGES, VSCSI_VPD_SUPPORTED_PAGES_SIZE+1);
typedef VSCSIVPDPAGESUPPORTEDPAGES *PVSCSIVPDPAGESUPPORTEDPAGES;
typedef const VSCSIVPDPAGESUPPORTEDPAGES *PCVSCSIVPDPAGESUPPORTEDPAGES;

/** VPD block characteristics page number. */
#define VSCSI_VPD_BLOCK_CHARACTERISTICS_NUMBER 0xb1
/** VPD block characteristics size. */
#define VSCSI_VPD_BLOCK_CHARACTERISTICS_SIZE   64
/**
 * Block limits VPD page data.
 */
#pragma pack(1)
typedef struct VSCSIVPDPAGEBLOCKCHARACTERISTICS
{
    /** Device type. */
    unsigned u5PeripheralDeviceType : 5;    /**< 0x00 / 00 */
    /** Qualifier. */
    unsigned u3PeripheralQualifier  : 3;
    /** Page number. */
    unsigned u8PageCode             : 8;
    /** Page size (Big endian) */
    unsigned u16PageLength          : 16;
    /** Medium rotation rate. */
    unsigned u16MediumRotationRate  : 16;
    /** Reserved. */
    unsigned u8Reserved             : 8;
    /** Nominal form factor. */
    unsigned u4NominalFormFactor    : 4;
    /** Reserved */
    unsigned u4Reserved             : 4;
    /** Reserved. */
    uint8_t  abReserved[56];
} VSCSIVPDPAGEBLOCKCHARACTERISTICS;
#pragma pack()
AssertCompileSize(VSCSIVPDPAGEBLOCKCHARACTERISTICS, VSCSI_VPD_BLOCK_CHARACTERISTICS_SIZE);
typedef VSCSIVPDPAGEBLOCKCHARACTERISTICS *PVSCSIVPDPAGEBLOCKCHARACTERISTICS;
typedef const VSCSIVPDPAGEBLOCKCHARACTERISTICS *PCVSCSIVPDPAGEBLOCKCHARACTERISTICS;

#define VSCSI_VPD_BLOCK_CHARACT_MEDIUM_ROTATION_RATE_NOT_REPORTED UINT16_C(0x0000)
#define VSCSI_VPD_BLOCK_CHARACT_MEDIUM_ROTATION_RATE_NON_ROTATING UINT16_C(0x0001)

/** VPD block limits page number. */
#define VSCSI_VPD_BLOCK_LIMITS_NUMBER 0xb0
/** VPD block limits size. */
#define VSCSI_VPD_BLOCK_LIMITS_SIZE   64
/**
 * Block limits VPD page data.
 */
#pragma pack(1)
typedef struct VSCSIVPDPAGEBLOCKLIMITS
{
    /** Device type. */
    unsigned u5PeripheralDeviceType : 5;    /**< 0x00 / 00 */
    /** Qualifier. */
    unsigned u3PeripheralQualifier  : 3;
    /** Page number. */
    unsigned u8PageCode             : 8;
    /** Page size (Big endian) */
    unsigned u16PageLength          : 16;
    /** Reserved. */
    uint8_t  u8Reserved;
    /** Maximum compare and write length. */
    uint8_t  u8MaxCmpWriteLength;
    /** Optimal transfer length granularity. */
    uint16_t u16OptTrfLengthGran;
    /** Maximum transfer length. */
    uint32_t u32MaxTrfLength;
    /** Optimal transfer length. */
    uint32_t u32OptTrfLength;
    /** Maximum PREFETCH, XDREAD and XDWRITE transfer length. */
    uint32_t u32MaxPreXdTrfLength;
    /** Maximum UNMAP LBA count. */
    uint32_t u32MaxUnmapLbaCount;
    /** Maximum UNMAP block descriptor count. */
    uint32_t u32MaxUnmapBlkDescCount;
    /** Optimal UNMAP granularity. */
    uint32_t u32OptUnmapGranularity;
    /** UNMAP granularity alignment. */
    uint32_t u32UnmapGranularityAlignment;
    /** Reserved. */
    uint8_t  abReserved[28];
} VSCSIVPDPAGEBLOCKLIMITS;
#pragma pack()
AssertCompileSize(VSCSIVPDPAGEBLOCKLIMITS, VSCSI_VPD_BLOCK_LIMITS_SIZE);
typedef VSCSIVPDPAGEBLOCKLIMITS *PVSCSIVPDPAGEBLOCKLIMITS;
typedef const VSCSIVPDPAGEBLOCKLIMITS *PCVSCSIVPDPAGEBLOCKLIMITS;

/** VPD block provisioning page number. */
#define VSCSI_VPD_BLOCK_PROV_NUMBER 0xb2
/** VPD block provisioning size. */
#define VSCSI_VPD_BLOCK_PROV_SIZE   8
/**
 * Block provisioning VPD page data.
 */
#pragma pack(1)
typedef struct VSCSIVPDPAGEBLOCKPROV
{
    /** Device type. */
    unsigned u5PeripheralDeviceType : 5;    /**< 0x00 / 00 */
    /** Qualifier. */
    unsigned u3PeripheralQualifier  : 3;
    /** Page number. */
    unsigned u8PageCode             : 8;
    /** Page size (Big endian) */
    unsigned u16PageLength          : 16;
    /** Threshold exponent. */
    unsigned u8ThresholdExponent    : 8;
    /** Descriptor present. */
    unsigned fDP                    : 1;
    /** Anchored LBAs supported. */
    unsigned fAncSup                : 1;
    /** Reserved. */
    unsigned u4Reserved             : 4;
    /** WRITE SAME command supported. */
    unsigned fLBPWS                 : 1;
    /** UNMAP command supported. */
    unsigned fLBPU                  : 1;
    /** Provisioning type. */
    unsigned u3ProvType             : 3;
    /** Reserved. */
    unsigned u5Reserved             : 5;
    /** Reserved. */
    unsigned u8Reserved             : 8;
} VSCSIVPDPAGEBLOCKPROV;
#pragma pack()
AssertCompileSize(VSCSIVPDPAGEBLOCKPROV, VSCSI_VPD_BLOCK_PROV_SIZE);
typedef VSCSIVPDPAGEBLOCKPROV *PVSCSIVPDPAGEBLOCKPROV;
typedef const VSCSIVPDPAGEBLOCKPROV *PCVSCSIVPDPAGEBLOCKPROVS;

#endif /* !VBOX_INCLUDED_SRC_Storage_VSCSI_VSCSIVpdPages_h */

