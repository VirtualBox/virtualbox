/* $Id: bmpcache.h 114912 2026-08-10 12:03:20Z vitali.pelenjow@oracle.com $ */
/** @file
 * VBox Remote Desktop Protocol.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

#ifndef VRDP_INCLUDED_SRC_bmpcache_h
#define VRDP_INCLUDED_SRC_bmpcache_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "vrdpdefs.h"
#include <VBox/RemoteDesktop/VRDEOrders.h>

/* An opaque 16 bytes heap handle. */
typedef struct BCHEAPHANDLE
{
    uint8_t au8[16];
} BCHEAPHANDLE;
AssertCompileSize(BCHEAPHANDLE, 16);

/* The Bitmap Cache forward declaration. */
struct _BMPCACHE;
typedef struct _BMPCACHE *PBMPCACHE;

/* The Bitmap Cache Entry forward declaration. */
struct _BMPCACHEENTRY;
typedef struct _BMPCACHEENTRY *PBMPCACHEENTRY;

typedef struct _BCTILEADDRESS
{
    VRDEBITMAPHASH *pHash; /* The pointer to the bitmap hash. Addresses exist only in the
                            * context of the bitmap. The address points to the hash variable
                            * inside the bitmap. It is to save memory.
                            */
    uint16_t u16TileX;     /* Horizontal tile coordinate */
    uint16_t u16TileY;     /* Vertical tile coordinate */
    void *pvServerData;    /* An opaque pointer used by the server for faster
                            * identification of the corresponding tile.
                            */
} BCTILEADDRESS;

typedef struct _BCTILEREMOTEID
{
    uint16_t id;           /* RDP cache identifier. */
    uint16_t idx;          /* RDP cache index. */
    void *pvClientData;    /* An opaque pointer used by the client for faster
                            * identification of the corresponding tile.
                            */
} BCTILEREMOTEID;

int BCCreate (PBMPCACHE *ppbc, size_t cbCache);
void BCDelete (PBMPCACHE pbc);

bool BCCacheBitmap (PBMPCACHE pbc, const BCHEAPHANDLE *pHandle, unsigned uScreenId);
void BCDeleteBitmap (PBMPCACHE pbc, const VRDEBITMAPHASH *pHash);

PBMPCACHEENTRY BCFindBitmap (PBMPCACHE pbc, const VRDEBITMAPHASH *pHash);

class VRDPBitmapCompressed;
VRDPBitmapCompressed *BCQueryBitmapCompressed (PBMPCACHEENTRY pbce);

int BCStore(BCHEAPHANDLE *pHandle, PBMPCACHE pbc, int32_t i32Op, const void *pvData, size_t cbData, const VRDEDATABITS *pBitsHdr, const uint8_t *pu8Bits, uint32_t u32ScreenAccessKey, uint32_t u32ScreenId);

void *BCBitmapHeapBlockQuery(PBMPCACHE pbc, const BCHEAPHANDLE *pHandle, int32_t i32Op, uint32_t *pcbBlock);
void BCBitmapHeapBlockRelease(PBMPCACHE pbc, const BCHEAPHANDLE *pHandle);
void BCBitmapHeapBlockFree(PBMPCACHE pbc, const BCHEAPHANDLE *pHandle);

DECLINLINE(bool) isValidBitsHdr(VRDEDATABITS const *pBitsHdr)
{
    switch (pBitsHdr->cbPixel)
    {
        case 2:
        case 3:
        case 4: break;

        default:
            return false;
    }

    /* uint16_t * uint16_t * uint8_t */
    uint64_t const u64BitmapSize = (uint64_t)pBitsHdr->cWidth * pBitsHdr->cHeight * pBitsHdr->cbPixel;
    if (pBitsHdr->cb < u64BitmapSize)
        return false;

    return true;
}

#endif /* !VRDP_INCLUDED_SRC_bmpcache_h */
