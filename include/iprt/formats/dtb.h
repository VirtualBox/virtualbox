/* $Id: dtb.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, DTB (Flattened Devicetree) format.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_dtb_h
#define IPRT_INCLUDED_formats_dtb_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/cdefs.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_dtb    Flattened Devicetree structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */

/**
 * The Flattened Devicetree Header.
 *
 * @note All fields are in big endian format.
 */
typedef struct DTBFDTHDR
{
    /** 0x00 - Magic value to identify a Flattened Devicetree header. */
    uint32_t                u32Magic;
    /** 0x04 - Total size of the devicetree structure in bytes. */
    uint32_t                cbFdt;
    /** 0x08 - Offset in bytes of the structure block from the beginning of this header. */
    uint32_t                offDtStruct;
    /** 0x0c - Offset in bytes of the strings block from the beginning of this header. */
    uint32_t                offDtStrings;
    /** 0x10 - Offset in bytes of the memory reservation block from the beginning of this header. */
    uint32_t                offMemRsvMap;
    /** 0x14 - Version of the devicetree data structure. */
    uint32_t                u32Version;
    /** 0x18 - Lowest version of the devicetree data structure with which the version used is backwards compatible. */
    uint32_t                u32VersionLastCompatible;
    /** 0x1c - Physical ID of the system's boot CPU. */
    uint32_t                u32CpuIdPhysBoot;
    /** 0x20 - Length of the strings block section in bytes. */
    uint32_t                cbDtStrings;
    /** 0x24 - Length of the structure block section in bytes. */
    uint32_t                cbDtStruct;
} DTBFDTHDR;
AssertCompileSize(DTBFDTHDR, 40);
/** Pointer to a Flattened Devicetree header. */
typedef DTBFDTHDR *PDTBFDTHDR;
/** Pointer to a constant Flattened Devicetree header. */
typedef const DTBFDTHDR *PCDTBFDTHDR;

/** The magic value for identifying a DTB header. */
#define DTBFDTHDR_MAGIC             UINT32_C(0xd00dfeed)
/** The current DTB header version. */
#define DTBFDTHDR_VERSION           17
/** The last compatible DTB header version. */
#define DTBFDTHDR_VERSION_LAST_COMP 16


/**
 * Memory reservation block entry.
 */
typedef struct DTBFDTRSVENTRY
{
    /** 0x00 - Physical address of the start of the reserved area. */
    uint64_t                PhysAddrStart;
    /** 0x08 - Size of the reserved area in bytes. */
    uint64_t                cbArea;
} DTBFDTRSVENTRY;
AssertCompileSize(DTBFDTRSVENTRY, 16);
/** Pointer to a memory reservation block entry. */
typedef DTBFDTRSVENTRY *PDTBFDTRSVENTRY;
/** Pointer to a constant memory reservation block entry. */
typedef const DTBFDTRSVENTRY *PCDTBFDTRSVENTRY;


/** @name Structure block related definitions.
 * @{ */
/** A begin node token. */
#define DTB_FDT_TOKEN_BEGIN_NODE                UINT32_C(0x00000001)
/** Big endian version of the begin node token as read from a data stream. */
#define DTB_FDT_TOKEN_BEGIN_NODE_BE             RT_H2BE_U32(DTB_FDT_TOKEN_BEGIN_NODE)
/** A end node token. */
#define DTB_FDT_TOKEN_END_NODE                  UINT32_C(0x00000002)
/** Big endian version of the end node token as read from a data stream. */
#define DTB_FDT_TOKEN_END_NODE_BE               RT_H2BE_U32(DTB_FDT_TOKEN_END_NODE)
/** A property token. */
#define DTB_FDT_TOKEN_PROPERTY                  UINT32_C(0x00000003)
/** Big endian version of the property token as read from a data stream. */
#define DTB_FDT_TOKEN_PROPERTY_BE               RT_H2BE_U32(DTB_FDT_TOKEN_PROPERTY)
/** A no-op token. */
#define DTB_FDT_TOKEN_NOP                       UINT32_C(0x00000004)
/** Big endian version of the no-op token as read from a data stream. */
#define DTB_FDT_TOKEN_NOP_BE                    RT_H2BE_U32(DTB_FDT_TOKEN_NOP)
/** The end token. */
#define DTB_FDT_TOKEN_END                       UINT32_C(0x00000009)
/** Big endian version of the end token as read from a data stream. */
#define DTB_FDT_TOKEN_END_BE                    RT_H2BE_U32(DTB_FDT_TOKEN_END)


/**
 * DTB_FDT_TOKEN_PROPERTY data followed after the token.
 */
typedef struct DTBFDTPROP
{
    /** 0x00 - Length of the property data in bytes (or 0 if not data). */
    uint32_t                cbProperty;
    /** 0x04 - Offset of the property name in the strings block. */
    uint32_t                offPropertyName;
} DTBFDTPROP;
AssertCompileSize(DTBFDTPROP, 8);
/** Pointer to the FDT property data. */
typedef DTBFDTPROP *PDTBFDTPROP;
/** Pointer to constant FDT property data. */
typedef const DTBFDTPROP *PCDTBFDTPROP;


/** @} */

/** @} */

#endif /* !IPRT_INCLUDED_formats_dtb_h */

