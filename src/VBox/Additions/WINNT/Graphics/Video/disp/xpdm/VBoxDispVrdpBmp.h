/* $Id: VBoxDispVrdpBmp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispVrdpBmp_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispVrdpBmp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* RDP cache holds about 350 tiles 64x64. Therefore
 * the driver does not have to cache more then the
 * RDP capacity. Most of bitmaps will be tiled, so
 * number of RDP tiles will be greater than number of
 * bitmaps. Also the number of bitmaps must be a power
 * of 2. So the 256 is a good number.
 */
#define VRDPBMP_N_CACHED_BITMAPS  (256)

#define VRDPBMP_RC_NOT_CACHED     (0x0000)
#define VRDPBMP_RC_CACHED         (0x0001)
#define VRDPBMP_RC_ALREADY_CACHED (0x0002)

#define VRDPBMP_RC_F_DELETED      (0x10000)

/* Bitmap hash. */
#pragma pack (1)
typedef struct _VRDPBCHASH
{
    /* A 64 bit hash value of pixels. */
    uint64_t hash64;

    /* Bitmap width. */
    uint16_t cx;

    /* Bitmap height. */
    uint16_t cy;

    /* Bytes per pixel at the bitmap. */
    uint8_t bytesPerPixel;

    /* Padding to 16 bytes. */
    uint8_t padding[3];
} VRDPBCHASH;
#pragma pack ()

#define VRDP_BC_ENTRY_STATUS_EMPTY     0
#define VRDP_BC_ENTRY_STATUS_TEMPORARY 1
#define VRDP_BC_ENTRY_STATUS_CACHED    2

typedef struct _VRDPBCENTRY
{
    struct _VRDPBCENTRY *next;
    struct _VRDPBCENTRY *prev;
    VRDPBCHASH hash;
    uint32_t u32Status;
} VRDPBCENTRY;

typedef struct _VRDPBC
{
    VRDPBCENTRY *headTmp;
    VRDPBCENTRY *tailTmp;
    VRDPBCENTRY *headCached;
    VRDPBCENTRY *tailCached;
    VRDPBCENTRY aEntries[VRDPBMP_N_CACHED_BITMAPS];
} VRDPBC;

void vrdpbmpReset (VRDPBC *pCache);
int vrdpbmpCacheSurface (VRDPBC *pCache, const SURFOBJ *pso, VRDPBCHASH *phash, VRDPBCHASH *phashDeleted, BOOL bForce);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispVrdpBmp_h */
