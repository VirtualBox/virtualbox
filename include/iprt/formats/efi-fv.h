/* $Id: efi-fv.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, EFI firmware volume (FV) definitions.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_efi_fv_h
#define IPRT_INCLUDED_formats_efi_fv_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>
#include <iprt/formats/efi-common.h>


/*
 * Definitions come from the UEFI PI Spec 1.5 Volume 3 Firmware, chapter 3 "Firmware Storage Code Definitions"
 */

/**
 * The volume header.
 */
typedef struct EFI_FIRMWARE_VOLUME_HEADER
{
    /** Reserved data for the reset vector. */
    uint8_t         abZeroVec[16];
    /** The filesystem GUID. */
    EFI_GUID        GuidFilesystem;
    /** The firmware volume length in bytes including this header. */
    uint64_t        cbFv;
    /** The signature of the firmware volume header (set to _FVH). */
    uint32_t        u32Signature;
    /** Firmware volume attributes. */
    uint32_t        fAttr;
    /** Size of the header in bytes. */
    uint16_t        cbFvHdr;
    /** Checksum of the header. */
    uint16_t        u16Chksum;
    /** Offset of the extended header (0 for no extended header). */
    uint16_t        offExtHdr;
    /** Reserved MBZ. */
    uint8_t         bRsvd;
    /** Revision of the header. */
    uint8_t         bRevision;
} EFI_FIRMWARE_VOLUME_HEADER;
AssertCompileSize(EFI_FIRMWARE_VOLUME_HEADER, 56);
/** Pointer to a EFI firmware volume header. */
typedef EFI_FIRMWARE_VOLUME_HEADER *PEFI_FIRMWARE_VOLUME_HEADER;
/** Pointer to a const EFI firmware volume header. */
typedef const EFI_FIRMWARE_VOLUME_HEADER *PCEFI_FIRMWARE_VOLUME_HEADER;

/** The signature for a firmware volume header. */
#define EFI_FIRMWARE_VOLUME_HEADER_SIGNATURE RT_MAKE_U32_FROM_U8('_', 'F', 'V', 'H')
/** Revision of the firmware volume header. */
#define EFI_FIRMWARE_VOLUME_HEADER_REVISION  2


/**
 * Firmware block map entry.
 */
typedef struct EFI_FW_BLOCK_MAP
{
    /** Number of blocks for this entry. */
    uint32_t        cBlocks;
    /** Block size in bytes. */
    uint32_t        cbBlock;
} EFI_FW_BLOCK_MAP;
AssertCompileSize(EFI_FW_BLOCK_MAP, 8);
/** Pointer to a firmware volume block map entry. */
typedef EFI_FW_BLOCK_MAP *PEFI_FW_BLOCK_MAP;
/** Pointer to a const firmware volume block map entry. */
typedef const EFI_FW_BLOCK_MAP *PCEFI_FW_BLOCK_MAP;


/**
 * Fault tolerant working block header.
 */
typedef struct EFI_FTW_BLOCK_HEADER
{
    /** GUID identifying the FTW block header. */
    EFI_GUID        GuidSignature;
    /** The checksum. */
    uint32_t        u32Chksum;
    /** Flags marking the working block area as valid/invalid. */
    uint32_t        fWorkingBlockValid;
    /** Size of the write queue. */
    uint64_t        cbWriteQueue;
} EFI_FTW_BLOCK_HEADER;
/** Pointer to a fault tolerant working block header. */
typedef EFI_FTW_BLOCK_HEADER *PEFI_FTW_BLOCK_HEADER;
/** Pointer to a const fault tolerant working block header. */
typedef const EFI_FTW_BLOCK_HEADER *PCEFI_FTW_BLOCK_HEADER;

/** The signature for the working block header. */
#define EFI_WORKING_BLOCK_SIGNATURE_GUID \
    { 0x9e58292b, 0x7c68, 0x497d, { 0xa0, 0xce, 0x65,  0x0, 0xfd, 0x9f, 0x1b, 0x95 }}

#endif /* !IPRT_INCLUDED_formats_efi_fv_h */

