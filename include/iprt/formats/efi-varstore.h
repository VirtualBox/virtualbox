/* $Id: efi-varstore.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, EFI variable store (VarStore) definitions.
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

#ifndef IPRT_INCLUDED_formats_efi_varstore_h
#define IPRT_INCLUDED_formats_efi_varstore_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>
#include <iprt/formats/efi-common.h>


/*
 * Definitions come from the EDK2 sources MdeModulePkg/Include/Guid/VariableFormat.h
 */

/** The filesystem GUID for a variable store stored in a volume header. */
#define EFI_VARSTORE_FILESYSTEM_GUID \
    { 0xfff12b8d, 0x7696, 0x4c8b, { 0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50 }}


/**
 * The variable store header.
 */
typedef struct EFI_VARSTORE_HEADER
{
    /** The GUID identifying a variable store. */
    EFI_GUID        GuidVarStore;
    /** Size of the variable store including the header. */
    uint32_t        cbVarStore;
    /** The format state. */
    uint8_t         bFmt;
    /** The region health state. */
    uint8_t         bState;
    /** Reserved. */
    uint8_t         abRsvd[6];
} EFI_VARSTORE_HEADER;
AssertCompileSize(EFI_VARSTORE_HEADER, 28);
/** Pointer to a variable store header. */
typedef EFI_VARSTORE_HEADER *PEFI_VARSTORE_HEADER;
/** Pointer to a const variable store header. */
typedef const EFI_VARSTORE_HEADER *PCEFI_VARSTORE_HEADER;

/** The GUID for a variable store using the authenticated variable header format. */
#define EFI_VARSTORE_HEADER_GUID_AUTHENTICATED_VARIABLE \
    { 0xaaf32c78, 0x947b, 0x439a, { 0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92 } }
/** The GUID for a variable store using the standard variable header format. */
#define EFI_VARSTORE_HEADER_GUID_VARIABLE \
  { 0xddcf3616, 0x3275, 0x4164, { 0x98, 0xb6, 0xfe, 0x85, 0x70, 0x7f, 0xfe, 0x7d } }

/** The EFI_VARSTORE_HEADER::bFmt value when the store region is formatted. */
#define EFI_VARSTORE_HEADER_FMT_FORMATTED           0x5a
/** The EFI_VARSTORE_HEADER::bState value when the store region is healthy. */
#define EFI_VARSTORE_HEADER_STATE_HEALTHY           0xfe


/**
 * Authenticated variable header.
 */
#pragma pack(1)
typedef struct EFI_AUTH_VAR_HEADER
{
    /** Contains EFI_AUTH_VAR_HEADER_START to identify the start of a new variable header. */
    uint16_t        u16StartId;
    /** Variable state. */
    uint8_t         bState;
    /** Reserved. */
    uint8_t         bRsvd;
    /** Variable attributes. */
    uint32_t        fAttr;
    /** Monotonic counter value increased with each change to protect against replay attacks. */
    uint64_t        cMonotonic;
    /** Timestamp value to protect against replay attacks. */
    EFI_TIME        Timestamp;
    /** Index of associated public key in database. */
    uint32_t        idPubKey;
    /** Size of the variable zero terminated unicode name in bytes. */
    uint32_t        cbName;
    /** Size of the variable data without this header. */
    uint32_t        cbData;
    /** Producer/Consumer GUID for this variable. */
    EFI_GUID        GuidVendor;
} EFI_AUTH_VAR_HEADER;
#pragma pack()
AssertCompileSize(EFI_AUTH_VAR_HEADER, 60);
/** Pointer to a authenticated variable header. */
typedef EFI_AUTH_VAR_HEADER *PEFI_AUTH_VAR_HEADER;
/** Pointer to a const authenticated variable header. */
typedef const EFI_AUTH_VAR_HEADER *PCEFI_AUTH_VAR_HEADER;

/** Value in EFI_AUTH_VAR_HEADER::u16StartId for a valid variable header. */
#define EFI_AUTH_VAR_HEADER_START                               0x55aa
/** @name Possible variable states.
 * @{ */
/** Variable is in the process of being deleted. */
#define EFI_AUTH_VAR_HEADER_STATE_IN_DELETED_TRANSITION         0xfe
/** Variable was deleted. */
#define EFI_AUTH_VAR_HEADER_STATE_DELETED                       0xfd
/** Variable has only a valid header right now. */
#define EFI_AUTH_VAR_HEADER_STATE_HDR_VALID_ONLY                0x7f
/** Variable header, name and data are all valid. */
#define EFI_AUTH_VAR_HEADER_STATE_ADDED                         0x3f
/** @} */


/** @name Possible variable attributes.
 * @{ */
/** The variable is stored in non volatile memory. */
#define EFI_VAR_HEADER_ATTR_NON_VOLATILE                        RT_BIT_32(0)
/** The variable is accessible by the EFI bootservice stage. */
#define EFI_VAR_HEADER_ATTR_BOOTSERVICE_ACCESS                  RT_BIT_32(1)
/** The variable is accessible during runtime. */
#define EFI_VAR_HEADER_ATTR_RUNTIME_ACCESS                      RT_BIT_32(2)
/** The variable contains an hardware error record. */
#define EFI_VAR_HEADER_ATTR_HW_ERROR_RECORD                     RT_BIT_32(3)
/** The variable can be modified only by an authenticated source. */
#define EFI_AUTH_VAR_HEADER_ATTR_AUTH_WRITE_ACCESS              RT_BIT_32(4)
/** The variable was written with a time based authentication. */
#define EFI_AUTH_VAR_HEADER_ATTR_TIME_BASED_AUTH_WRITE_ACCESS   RT_BIT_32(5)
/** The variable can be appended. */
#define EFI_AUTH_VAR_HEADER_ATTR_APPEND_WRITE                   RT_BIT_32(6)
/** @} */

#endif /* !IPRT_INCLUDED_formats_efi_varstore_h */

