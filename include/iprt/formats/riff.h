/* $Id: riff.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Resource Interchange File Format (RIFF), WAVE, ++.
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

#ifndef IPRT_INCLUDED_formats_riff_h
#define IPRT_INCLUDED_formats_riff_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_riff    RIFF & WAVE structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */

/**
 * Resource interchange file format (RIFF) file header.
 */
typedef struct RTRIFFHDR
{
    /** The 'RIFF' magic (RTRIFFHDR_MAGIC). */
    uint32_t        uMagic;
    /** The file size. */
    uint32_t        cbFile;
    /** The file type. */
    uint32_t        uFileType;
} RTRIFFHDR;
AssertCompileSize(RTRIFFHDR, 12);
/** Pointer to a RIFF file header. */
typedef RTRIFFHDR *PRTRIFFHDR;

/** Magic value for RTRIFFHDR::uMagic ('RIFF'). */
#define RTRIFFHDR_MAGIC         RT_BE2H_U32_C(0x52494646)

/** @name RIFF file types (RTRIFFHDR::uFileType)
 * @{ */
/** RIFF file type: WAVE (audio) */
#define RTRIFF_FILE_TYPE_WAVE   RT_BE2H_U32_C(0x57415645)
/** RIFF file type: AVI (video) */
#define RTRIFF_FILE_TYPE_AVI    RT_BE2H_U32_C(0x41564920)
/** @} */

/**
 * A RIFF chunk.
 */
typedef struct RTRIFFCHUNK
{
    /** The chunk magic (four character code). */
    uint32_t        uMagic;
    /** The size of the chunk minus this header. */
    uint32_t        cbChunk;
} RTRIFFCHUNK;
AssertCompileSize(RTRIFFCHUNK, 8);
/** Pointer to a RIFF chunk. */
typedef RTRIFFCHUNK *PRTRIFFCHUNK;

/**
 * A RIFF list.
 */
typedef struct RTRIFFLIST
{
    /** The list indicator (RTRIFFLIST_MAGIC). */
    uint32_t        uMagic;
    /** The size of the chunk minus this header. */
    uint32_t        cbChunk;
    /** The list type (four character code). */
    uint32_t        uListType;
} RTRIFFLIST;
AssertCompileSize(RTRIFFLIST, 12);
/** Pointer to a RIFF list. */
typedef RTRIFFLIST *PRTRIFFLIST;
/** Magic value for RTRIFFLIST::uMagic ('LIST'). */
#define RTRIFFLIST_MAGIC    RT_BE2H_U32_C(0x4c495354)

/** Generic 'INFO' list type. */
#define RTRIFFLIST_TYPE_INFO    RT_BE2H_U32_C(0x494e464f)


/**
 * Wave file format (WAVEFORMATEX w/o cbSize).
 * @see RTRIFFWAVEFMTCHUNK.
 */
typedef struct RTRIFFWAVEFMT
{
    /** Audio format tag. */
    uint16_t        uFormatTag;
    /** Number of channels. */
    uint16_t        cChannels;
    /** Sample rate. */
    uint32_t        uHz;
    /** Byte rate (= uHz * cChannels * cBitsPerSample / 8) */
    uint32_t        cbRate;
    /** Frame size (aka block alignment).    */
    uint16_t        cbFrame;
    /** Number of bits per sample. */
    uint16_t        cBitsPerSample;
} RTRIFFWAVEFMT;
AssertCompileSize(RTRIFFWAVEFMT, 16);
/** Pointer to a wave file format structure. */
typedef RTRIFFWAVEFMT *PRTRIFFWAVEFMT;

/**
 * Extensible wave file format (WAVEFORMATEXTENSIBLE).
 * @see RTRIFFWAVEFMTEXTCHUNK.
 */
#pragma pack(4) /* Override the uint64_t effect from RTUUID, so we can safely put it after RTRIFFHDR in a structure.   */
typedef struct RTRIFFWAVEFMTEXT
{
    /** The coreformat structure. */
    RTRIFFWAVEFMT       Core;
    /** Number of bytes of extra information after the core. */
    uint16_t            cbExtra;
    /** Number of valid bits per sample. */
    uint16_t            cValidBitsPerSample;
    /** The channel mask. */
    uint32_t            fChannelMask;
    /** The GUID of the sub-format. */
    RTUUID              SubFormat;
} RTRIFFWAVEFMTEXT;
#pragma pack()
AssertCompileSize(RTRIFFWAVEFMTEXT, 16+2+22);
/** Pointer to an extensible wave file format structure. */
typedef RTRIFFWAVEFMTEXT *PRTRIFFWAVEFMTEXT;

/** RTRIFFWAVEFMT::uFormatTag value for PCM (WDK: WAVE_FORMAT_PCM). */
#define RTRIFFWAVEFMT_TAG_PCM           UINT16_C(0x0001)
/** RTRIFFWAVEFMT::uFormatTag value for extensible wave files (WDK: WAVE_FORMAT_EXTENSIBLE). */
#define RTRIFFWAVEFMT_TAG_EXTENSIBLE    UINT16_C(0xfffe)

/** Typical RTRIFFWAVEFMTEXT::cbExtra value (min). */
#define RTRIFFWAVEFMTEXT_EXTRA_SIZE     UINT16_C(22)

/** @name Channel IDs for RTRIFFWAVEFMTEXT::fChannelMask.
 * @{ */
#define RTRIFFWAVEFMTEXT_CH_ID_FL       RT_BIT_32(0)    /**< Front left. */
#define RTRIFFWAVEFMTEXT_CH_ID_FR       RT_BIT_32(1)    /**< Front right. */
#define RTRIFFWAVEFMTEXT_CH_ID_FC       RT_BIT_32(2)    /**< Front center */
#define RTRIFFWAVEFMTEXT_CH_ID_LFE      RT_BIT_32(3)    /**< Low frequency */
#define RTRIFFWAVEFMTEXT_CH_ID_BL       RT_BIT_32(4)    /**< Back left. */
#define RTRIFFWAVEFMTEXT_CH_ID_BR       RT_BIT_32(5)    /**< Back right. */
#define RTRIFFWAVEFMTEXT_CH_ID_FLC      RT_BIT_32(6)    /**< Front left of center. */
#define RTRIFFWAVEFMTEXT_CH_ID_FLR      RT_BIT_32(7)    /**< Front right of center. */
#define RTRIFFWAVEFMTEXT_CH_ID_BC       RT_BIT_32(8)    /**< Back center. */
#define RTRIFFWAVEFMTEXT_CH_ID_SL       RT_BIT_32(9)    /**< Side left. */
#define RTRIFFWAVEFMTEXT_CH_ID_SR       RT_BIT_32(10)   /**< Side right. */
#define RTRIFFWAVEFMTEXT_CH_ID_TC       RT_BIT_32(11)   /**< Top center. */
#define RTRIFFWAVEFMTEXT_CH_ID_TFL      RT_BIT_32(12)   /**< Top front left. */
#define RTRIFFWAVEFMTEXT_CH_ID_TFC      RT_BIT_32(13)   /**< Top front center. */
#define RTRIFFWAVEFMTEXT_CH_ID_TFR      RT_BIT_32(14)   /**< Top front right. */
#define RTRIFFWAVEFMTEXT_CH_ID_TBL      RT_BIT_32(15)   /**< Top back left. */
#define RTRIFFWAVEFMTEXT_CH_ID_TBC      RT_BIT_32(16)   /**< Top back center. */
#define RTRIFFWAVEFMTEXT_CH_ID_TBR      RT_BIT_32(17)   /**< Top back right. */
/** @} */

/** RTRIFFWAVEFMTEXT::SubFormat UUID string for PCM. */
#define RTRIFFWAVEFMTEXT_SUBTYPE_PCM "00000001-0000-0010-8000-00aa00389b71"


/**
 * Wave file format chunk.
 */
typedef struct RTRIFFWAVEFMTCHUNK
{
    /** Chunk header with RTRIFFWAVEFMT_MAGIC as magic. */
    RTRIFFCHUNK     Chunk;
    /** The wave file format. */
    RTRIFFWAVEFMT   Data;
} RTRIFFWAVEFMTCHUNK;
AssertCompileSize(RTRIFFWAVEFMTCHUNK, 8+16);
/** Pointer to a wave file format chunk.   */
typedef RTRIFFWAVEFMTCHUNK *PRTRIFFWAVEFMTCHUNK;
/** Magic value for RTRIFFWAVEFMTCHUNK and RTRIFFWAVEFMTEXTCHUNK ('fmt '). */
#define RTRIFFWAVEFMT_MAGIC RT_BE2H_U32_C(0x666d7420)

/**
 * Extensible wave file format chunk.
 */
typedef struct RTRIFFWAVEFMTEXTCHUNK
{
    /** Chunk header with RTRIFFWAVEFMT_MAGIC as magic. */
    RTRIFFCHUNK         Chunk;
    /** The wave file format. */
    RTRIFFWAVEFMTEXT    Data;
} RTRIFFWAVEFMTEXTCHUNK;
AssertCompileSize(RTRIFFWAVEFMTEXTCHUNK, 8+16+2+22);
/** Pointer to a wave file format chunk.   */
typedef RTRIFFWAVEFMTEXTCHUNK *PRTRIFFWAVEFMTEXTCHUNK;


/**
 * Wave file data chunk.
 */
typedef struct RTRIFFWAVEDATACHUNK
{
    /** Chunk header with RTRIFFWAVEFMT_MAGIC as magic. */
    RTRIFFCHUNK     Chunk;
    /** Variable sized sample data. */
    uint8_t         abData[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} RTRIFFWAVEDATACHUNK;

/** Magic value for RTRIFFWAVEFMT::uMagic ('data'). */
#define RTRIFFWAVEDATACHUNK_MAGIC   RT_BE2H_U32_C(0x64617461)


/** Magic value padding chunks ('PAD '). */
#define RTRIFFPADCHUNK_MAGIC        RT_BE2H_U32_C(0x50414420)

/** @} */

#endif /* !IPRT_INCLUDED_formats_riff_h */

