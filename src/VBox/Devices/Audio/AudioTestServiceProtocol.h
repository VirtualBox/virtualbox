/* $Id: AudioTestServiceProtocol.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * AudioTestServiceProtocol - Audio test execution server, Protocol Header.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VBOX_INCLUDED_SRC_Audio_AudioTestServiceProtocol_h
#define VBOX_INCLUDED_SRC_Audio_AudioTestServiceProtocol_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/list.h>

#include <VBox/vmm/pdmaudioifs.h>

#include "AudioTest.h"

RT_C_DECLS_BEGIN

/** Maximum length (in bytes) an opcode can have. */
#define ATSPKT_OPCODE_MAX_LEN           8
/** Packet alignment. */
#define ATSPKT_ALIGNMENT                16
/** Max packet size. */
#define ATSPKT_MAX_SIZE                 _256K

/**
 * Common Packet header (for requests and replies).
 */
typedef struct ATSPKTHDR
{
    /** The unpadded packet length. This include this header. */
    uint32_t        cb;
    /** The CRC-32 for the packet starting from the opcode field.  0 if the packet
     *  hasn't been CRCed. */
    uint32_t        uCrc32;
    /** Packet opcode, an unterminated ASCII string.  */
    uint8_t         achOpcode[ATSPKT_OPCODE_MAX_LEN];
} ATSPKTHDR;
AssertCompileSize(ATSPKTHDR, 16);
/** Pointer to a packet header. */
typedef ATSPKTHDR *PATSPKTHDR;
/** Pointer to a packet header. */
typedef ATSPKTHDR const *PCATSPKTHDR;
/** Pointer to a packet header pointer. */
typedef PATSPKTHDR *PPATSPKTHDR;

#define ATSPKT_OPCODE_HOWDY             "HOWDY   "

/** 32bit protocol version consisting of a 16bit major and 16bit minor part. */
#define ATS_PROTOCOL_VS (ATS_PROTOCOL_VS_MAJOR | ATS_PROTOCOL_VS_MINOR)
/** The major version part of the protocol version. */
#define ATS_PROTOCOL_VS_MAJOR (1 << 16)
/** The minor version part of the protocol version. */
#define ATS_PROTOCOL_VS_MINOR (0)

/**
 * The HOWDY request structure.
 */
typedef struct ATSPKTREQHOWDY
{
    /** Embedded packet header. */
    ATSPKTHDR       Hdr;
    /** Version of the protocol the client wants to use. */
    uint32_t        uVersion;
    /** Alignment. */
    uint8_t         au8Padding[12];
} ATSPKTREQHOWDY;
AssertCompileSizeAlignment(ATSPKTREQHOWDY, ATSPKT_ALIGNMENT);
/** Pointer to a HOWDY request structure. */
typedef ATSPKTREQHOWDY *PATSPKTREQHOWDY;

/**
 * The HOWDY reply structure.
 */
typedef struct ATSPKTREPHOWDY
{
    /** Packet header. */
    ATSPKTHDR       Hdr;
    /** Version to use for the established connection. */
    uint32_t        uVersion;
    /** Padding - reserved. */
    uint8_t         au8Padding[12];
} ATSPKTREPHOWDY;
AssertCompileSizeAlignment(ATSPKTREPHOWDY, ATSPKT_ALIGNMENT);
/** Pointer to a HOWDY reply structure. */
typedef ATSPKTREPHOWDY *PATSPKTREPHOWDY;

#define ATSPKT_OPCODE_BYE               "BYE     "

/* No additional structures for BYE. */

#define ATSPKT_OPCODE_TESTSET_BEGIN     "TSET BEG"

/**
 * The TSET BEG (test set begin) request structure.
 */
typedef struct ATSPKTREQTSETBEG
{
    /** Embedded packet header. */
    ATSPKTHDR          Hdr;
    /** Audio test set tag to use. */
    char               szTag[AUDIOTEST_TAG_MAX];
} ATSPKTREQTSETBEG;
AssertCompileSizeAlignment(ATSPKTREQTSETBEG, ATSPKT_ALIGNMENT);
/** Pointer to a TSET BEG reply structure. */
typedef ATSPKTREQTSETBEG *PATSPKTREQTSETBEG;

#define ATSPKT_OPCODE_TESTSET_END       "TSET END"

/**
 * The TSET END (test set end) request structure.
 */
typedef struct ATSPKTREQTSETEND
{
    /** Embedded packet header. */
    ATSPKTHDR          Hdr;
    /** Audio test set tag to use. */
    char               szTag[AUDIOTEST_TAG_MAX];
} ATSPKTREQTSETEND;
AssertCompileSizeAlignment(ATSPKTREQTSETEND, ATSPKT_ALIGNMENT);
/** Pointer to a TSET STA reply structure. */
typedef ATSPKTREQTSETEND *PATSPKTREQTSETEND;

#define ATSPKT_OPCODE_TESTSET_SEND      "TSET SND"

/**
 * The TSET SND (test set send) request structure.
 */
typedef struct ATSPKTREQTSETSND
{
    /** Embedded packet header. */
    ATSPKTHDR          Hdr;
    /** Audio test set tag to use. */
    char               szTag[AUDIOTEST_TAG_MAX];
} ATSPKTREQTSETSND;
AssertCompileSizeAlignment(ATSPKTREQTSETSND, ATSPKT_ALIGNMENT);
/** Pointer to a ATSPKTREQTSETSND structure. */
typedef ATSPKTREQTSETSND *PATSPKTREQTSETSND;

#define ATSPKT_OPCODE_TONE_PLAY         "TN PLY  "

/**
 * The TN PLY (tone play) request structure.
 */
typedef struct ATSPKTREQTONEPLAY
{
    /** Embedded packet header. */
    ATSPKTHDR          Hdr;
    /** Test tone parameters for playback. */
    AUDIOTESTTONEPARMS ToneParms;
#if ARCH_BITS == 64
    uint8_t            aPadding[8];
#else
# ifdef RT_OS_WINDOWS
    uint8_t            aPadding[4];
# else
    uint8_t            aPadding[12];
# endif
#endif
} ATSPKTREQTONEPLAY;
AssertCompileSizeAlignment(ATSPKTREQTONEPLAY, ATSPKT_ALIGNMENT);
/** Pointer to a ATSPKTREQTONEPLAY structure. */
typedef ATSPKTREQTONEPLAY *PATSPKTREQTONEPLAY;

#define ATSPKT_OPCODE_TONE_RECORD       "TN REC  "

/**
 * The TN REC (tone record) request structure.
 */
typedef struct ATSPKTREQTONEREC
{
    /** Embedded packet header. */
    ATSPKTHDR          Hdr;
    /** Test tone parameters for playback. */
    AUDIOTESTTONEPARMS ToneParms;
#if ARCH_BITS == 64
    uint8_t            aPadding[8];
#else
# ifdef RT_OS_WINDOWS
    uint8_t            aPadding[4];
# else
    uint8_t            aPadding[12];
# endif
#endif
} ATSPKTREQTONEREC;
AssertCompileSizeAlignment(ATSPKTREQTONEREC, ATSPKT_ALIGNMENT);
/** Pointer to a ATSPKTREQTONEREC structure. */
typedef ATSPKTREQTONEREC *PATSPKTREQTONEREC;

/* No additional structure for the reply (just standard STATUS packet). */

/**
 * The failure reply structure.
 */
typedef struct ATSPKTREPFAIL
{
    /** Embedded packet header. */
    ATSPKTHDR   Hdr;
    /** Error code (IPRT-style). */
    int         rc;
    /** Error description. */
    char        ach[256];
} ATSPKTREPFAIL;
/** Pointer to a ATSPKTREPFAIL structure. */
typedef ATSPKTREPFAIL *PATSPKTREPFAIL;

/**
 * Checks if the two opcodes match.
 *
 * @returns true on match, false on mismatch.
 * @param   pPktHdr             The packet header.
 * @param   pszOpcode2          The opcode we're comparing with.  Does not have
 *                              to be the whole 8 chars long.
 */
DECLINLINE(bool) atsIsSameOpcode(PCATSPKTHDR pPktHdr, const char *pszOpcode2)
{
    if (pPktHdr->achOpcode[0] != pszOpcode2[0])
        return false;
    if (pPktHdr->achOpcode[1] != pszOpcode2[1])
        return false;

    unsigned i = 2;
    while (   i < RT_SIZEOFMEMB(ATSPKTHDR, achOpcode)
           && pszOpcode2[i] != '\0')
    {
        if (pPktHdr->achOpcode[i] != pszOpcode2[i])
            break;
        i++;
    }

    if (   i < RT_SIZEOFMEMB(ATSPKTHDR, achOpcode)
        && pszOpcode2[i] == '\0')
    {
        while (   i < RT_SIZEOFMEMB(ATSPKTHDR, achOpcode)
               && pPktHdr->achOpcode[i] == ' ')
            i++;
    }

    return i == RT_SIZEOFMEMB(ATSPKTHDR, achOpcode);
}

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Audio_AudioTestServiceProtocol_h */
