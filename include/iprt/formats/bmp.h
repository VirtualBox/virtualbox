/* $Id: bmp.h 109819 2025-06-11 12:09:40Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT - Microsoft Bitmap Formats (BMP).
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_bmp_h
#define IPRT_INCLUDED_formats_bmp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_fmt_bmp  Microsoft Bitmaps Formats (BMP)
 * @ingroup grp_rt_formats
 * @{
 */

/** @name BMP header sizes (in bytes).
 * @{ */
#define BMP_HDR_SIZE_FILE      14
#define BMP_HDR_SIZE_OS21      12
#define BMP_HDR_SIZE_OS22      64
#define BMP_HDR_SIZE_WIN3X     40
#define BMP_HDR_SIZE_NT4       108
#define BMP_HDR_SIZE_W2K       124
/** @} */


/** BMP format file header. */
#pragma pack(1)
typedef struct BMPFILEHDR
{
    /** File type identifier ("magic"). */
    uint16_t      uType;
    /** Size of file in bytes. */
    uint32_t      cbFileSize;
    /** Reserved (should be 0). */
    uint16_t      Reserved1;
    /** Reserved (should be 0). */
    uint16_t      Reserved2;
    /** Offset (in bytes) to bitmap data. */
    uint32_t      offBits;
} BMPFILEHDR;
#pragma pack()
AssertCompileSize(BMPFILEHDR, BMP_HDR_SIZE_FILE);
/** Pointer to a BMP format file header. */
typedef BMPFILEHDR *PBMPFILEHDR;

/** BMP file magic number for BMP / DIB. */
#define BMP_HDR_MAGIC (RT_H2LE_U16_C(0x4d42))

/** OS/2 1.x BMP core header,
 *  also known as BITMAPCOREHEADER. */
typedef struct BMPOS2COREHDR
{
    /** Size (in bytes) of remaining header. */
    uint32_t      cbSize;
    /** Width of bitmap in pixels. */
    uint16_t      uWidth;
    /** Height of bitmap in pixels. */
    uint16_t      uHeight;
    /** Number of planes. */
    uint16_t      cPlanes;
    /** Color bits per pixel. */
    uint16_t      cBits;
} BMPOS2COREHDR;
AssertCompileSize(BMPOS2COREHDR, BMP_HDR_SIZE_OS21);
/** Pointer to a OS/2 1.x BMP core header. */
typedef BMPOS2COREHDR *PBMPOS2COREHDR;

/** OS/2 2.0 BMP core header, version 2,
 *  also known as BITMAPCOREHEADER2. */
typedef struct BMPOS2COREHDR2
{
    /** Size (in bytes) of remaining header. */
    uint32_t      cbSize;
    /** Width of bitmap in pixels. */
    uint32_t      uWidth;
    /** Height of bitmap in pixels. */
    uint32_t      uHeight;
    /** Number of planes. */
    uint16_t      cPlanes;
    /** Color bits per pixel. */
    uint16_t      cBits;
    /** Compression scheme of type BMP_COMPRESSION_TYPE. */
    uint32_t      enmCompression;
    /** Size of bitmap in bytes. */
    uint32_t      cbSizeImage;
    /** Horz. resolution in pixels/meter. */
    uint32_t      uXPelsPerMeter;
    /** Vert. resolution in pixels/meter. */
    uint32_t      uYPelsPerMeter;
    /** Number of colors in color table. */
    uint32_t      cClrUsed;
    /** Number of important colors. */
    uint32_t      cClrImportant;
    /** Resolution measurement Used. */
    uint16_t      uUnits;
    /** Reserved fields (always 0). */
    uint16_t      Reserved;
    /** Orientation of bitmap. */
    uint16_t      uRecording;
    /** Halftone algorithm used on image. */
    uint16_t      enmHalftone;
    /** Halftone algorithm data. */
    uint32_t      uHalftoneParm1;
    /** Halftone algorithm data. */
    uint32_t      uHalftoneParm2;
    /** Color table format (always 0). */
    uint32_t      uColorEncoding;
    /** Misc. field for application use  . */
    uint32_t      uIdentifier;
} BMPOS2COREHDR2;
AssertCompileSize(BMPOS2COREHDR2, BMP_HDR_SIZE_OS22);
/** Pointer to an OS/2 2.0 BMP core header version 2. */
typedef BMPOS2COREHDR2 *PBMPOS2COREHDR2;

/** Windows 3.x BMP information header Format. */
typedef struct BMPWIN3XINFOHDR
{
    /** Size (in bytes) of remaining header. */
    uint32_t      cbSize;
    /** Width of bitmap in pixels. */
    uint32_t      uWidth;
    /** Height of bitmap in pixels. */
    uint32_t      uHeight;
    /** Number of planes. */
    uint16_t      cPlanes;
    /** Color bits per pixel. */
    uint16_t      cBits;
    /** Compression scheme of type BMP_COMPRESSION_TYPE. */
    uint32_t      enmCompression;
    /** Size of bitmap in bytes. */
    uint32_t      cbSizeImage;
    /** Horz. resolution in pixels/meter. */
    uint32_t      uXPelsPerMeter;
    /** Vert. resolution in pixels/meter. */
    uint32_t      uYPelsPerMeter;
    /** Number of colors in color table. */
    uint32_t      cClrUsed;
    /** Number of important colors. */
    uint32_t      cClrImportant;
} BMPWIN3XINFOHDR;
AssertCompileSize(BMPWIN3XINFOHDR, BMP_HDR_SIZE_WIN3X);
/** Pointer to a Windows 3.x BMP information header. */
typedef BMPWIN3XINFOHDR *PBMPWIN3XINFOHDR;

typedef struct BMPWINCIEXYZ
{
    int32_t     x, y, z;
} BMPWINCIEXYZ;
AssertCompileSize(BMPWINCIEXYZ, 12);

typedef struct BMPWINCIEXYZTRIPLE
{
    BMPWINCIEXYZ Red, Green, Blue;
} BMPWINCIEXYZTRIPLE;
AssertCompileSize(BMPWINCIEXYZTRIPLE, 36);

typedef struct BMPWINV4INFOHDR
{
    uint32_t    cbSize;
    int32_t     cx;
    int32_t     cy;
    uint16_t    cPlanes;
    uint16_t    cBits;
    uint32_t    enmCompression; /**< Differs from BMPWIN3XINFOHDR::enmCompression? */
    uint32_t    cbImage;
    int32_t     cXPelsPerMeter;
    int32_t     cYPelsPerMeter;
    uint32_t    cColorsUsed;
    uint32_t    cColorsImportant;
    /* New v4 fields: */
    uint32_t    fRedMask;
    uint32_t    fGreenMask;
    uint32_t    fBlueMask;
    uint32_t    fAlphaMask;
    uint32_t    enmCSType;
    BMPWINCIEXYZTRIPLE Endpoints;
    uint32_t    uGammaRed;
    uint32_t    uGammaGreen;
    uint32_t    uGammaBlue;
} BMPWINV4INFOHDR;
AssertCompileSize(BMPWINV4INFOHDR, BMP_HDR_SIZE_NT4);

typedef struct BMPWINV5INFOHDR
{
    uint32_t    cbSize;
    int32_t     cx;
    int32_t     cy;
    uint16_t    cPlanes;
    uint16_t    cBits;
    uint32_t    enmCompression; /**< Differs from BMPWIN3XINFOHDR::enmCompression? */
    uint32_t    cbImage;
    int32_t     cXPelsPerMeter;
    int32_t     cYPelsPerMeter;
    uint32_t    cColorsUsed;
    uint32_t    cColorsImportant;
    /* New v4 fields: */
    uint32_t    fRedMask;
    uint32_t    fGreenMask;
    uint32_t    fBlueMask;
    uint32_t    fAlphaMask;
    uint32_t    enmCSType;
    BMPWINCIEXYZTRIPLE Endpoints;
    uint32_t    uGammaRed;
    uint32_t    uGammaGreen;
    uint32_t    uGammaBlue;
    /* New v5 fields: */
    uint32_t    fIntent;
    uint32_t    offProfileData;
    uint32_t    cbProfileData;
    uint32_t    uReserved;
} BMPWINV5INFOHDR;
AssertCompileSize(BMPWINV5INFOHDR, BMP_HDR_SIZE_W2K);



/** @name BMP compression types.
 * @{  */
#define BMP_COMPRESSION_TYPE_NONE               0
#define BMP_COMPRESSION_TYPE_RLE8               1
#define BMP_COMPRESSION_TYPE_RLE4               2
#define BMP_COMPRESSION_TYPE_BITFIELDS          3
#define BMP_COMPRESSION_TYPE_JPEG               4
#define BMP_COMPRESSION_TYPE_PNG                5
#define BMP_COMPRESSION_TYPE_ALPHABITFIELDS     6
#define BMP_COMPRESSION_TYPE_CMYK               11
#define BMP_COMPRESSION_TYPE_CMYKRLE8           12
#define BMP_COMPRESSION_TYPE_CMYKRLE4           13
/** @} */

/** @} */

#endif /* !IPRT_INCLUDED_formats_bmp_h */

