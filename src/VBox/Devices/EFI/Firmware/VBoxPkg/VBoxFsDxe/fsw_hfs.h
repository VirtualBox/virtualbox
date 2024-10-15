/* $Id: fsw_hfs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * fsw_hfs.h - HFS file system driver header.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef _FSW_HFS_H_
#define _FSW_HFS_H_

#define VOLSTRUCTNAME fsw_hfs_volume
#define DNODESTRUCTNAME fsw_hfs_dnode

#include "fsw_core.h"

#define IN_RING0
#if !defined(ARCH_BITS) || !defined(HC_ARCH_BITS)
# error "please add right bitness"
#endif
#include "iprt/formats/hfs.h"
#include "iprt/asm.h"           /* endian conversion */

#ifndef HOST_POSIX
#include <Library/BaseLib.h>
#endif

//! Block size for HFS volumes.
#define HFS_BLOCKSIZE            512

//! Block number where the HFS superblock resides.
#define HFS_SUPERBLOCK_BLOCKNO   2

#ifdef _MSC_VER
/* vasily: disable warning for non-standard anonymous struct/union
 * declarations
 */
# pragma warning (disable:4201)
#endif

struct hfs_dirrec {
    fsw_u8      _dummy;
};

#pragma pack(1)
struct fsw_hfs_key
{
  union
  {
    struct HFSPlusExtentKey  ext_key;
    struct HFSPlusCatalogKey cat_key;
    fsw_u16                  key_len; /* Length is at the beginning of all keys */
  };
};
#pragma pack()

typedef enum {
    /* Regular HFS */
    FSW_HFS_PLAIN = 0,
    /* HFS+ */
    FSW_HFS_PLUS,
    /* HFS+ embedded to HFS */
    FSW_HFS_PLUS_EMB
} fsw_hfs_kind;

/**
 * HFS: Dnode structure with HFS-specific data.
 */
struct fsw_hfs_dnode
{
  struct fsw_dnode          g;          //!< Generic dnode structure
  HFSPlusExtentRecord       extents;
  fsw_u32                   ctime;
  fsw_u32                   mtime;
  fsw_u64                   used_bytes;
  fsw_u32                   node_num;
};

/**
 * HFS: In-memory B-tree structure.
 */
struct fsw_hfs_btree
{
    fsw_u32                  root_node;
    fsw_u32                  node_size;
    struct fsw_hfs_dnode*    file;
};


/**
 * HFS: In-memory volume structure with HFS-specific data.
 */

struct fsw_hfs_volume
{
    struct fsw_volume            g;            //!< Generic volume structure

    struct HFSPlusVolumeHeader   *primary_voldesc;  //!< Volume Descriptor
    struct fsw_hfs_btree          catalog_tree;     // Catalog tree
    struct fsw_hfs_btree          extents_tree;     // Extents overflow tree
    struct fsw_hfs_dnode          root_file;
    int                           case_sensitive;
    fsw_u32                       block_size_shift;
    fsw_hfs_kind                  hfs_kind;
    fsw_u32                       emb_block_off;
};

/* Endianess swappers. */
DECLINLINE(fsw_u16)
be16_to_cpu(fsw_u16 x)
{
    return RT_BE2H_U16(x);
}

DECLINLINE(fsw_u16)
cpu_to_be16(fsw_u16 x)
{
    return RT_H2BE_U16(x);
}


DECLINLINE(fsw_u32)
cpu_to_be32(fsw_u32 x)
{
    return RT_H2BE_U32(x);
}

DECLINLINE(fsw_u32)
be32_to_cpu(fsw_u32 x)
{
    return RT_BE2H_U32(x);
}

DECLINLINE(fsw_u64)
be64_to_cpu(fsw_u64 x)
{
#ifdef RT_LITTLE_ENDIAN
#ifdef HOST_POSIX
    return RT_BE2H_U64(x);
#else
    return SwapBytes64(x);
#endif
#else
    return x;
#endif
}

#endif

