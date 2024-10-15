/* $Id: asn1-ut-string.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, XXX STRING Types.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/asn1.h>

#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/uni.h>

#include <iprt/formats/asn1.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static uint8_t const g_acbStringTags[] =
{
    /* [ASN1_TAG_EOC]               = */ 0,
    /* [ASN1_TAG_BOOLEAN]           = */ 0,
    /* [ASN1_TAG_INTEGER]           = */ 0,
    /* [ASN1_TAG_BIT_STRING]        = */ 0,
    /* [ASN1_TAG_OCTET_STRING]      = */ 0,
    /* [ASN1_TAG_NULL]              = */ 0,
    /* [ASN1_TAG_OID]               = */ 0,
    /* [ASN1_TAG_OBJECT_DESCRIPTOR] = */ 0,
    /* [ASN1_TAG_EXTERNAL]          = */ 0,
    /* [ASN1_TAG_REAL]              = */ 0,
    /* [ASN1_TAG_ENUMERATED]        = */ 0,
    /* [ASN1_TAG_EMBEDDED_PDV]      = */ 0,
    /* [ASN1_TAG_UTF8_STRING]       = */ 1,
    /* [ASN1_TAG_RELATIVE_OID]      = */ 0,
    /* [ASN1_TAG_RESERVED_14]       = */ 0,
    /* [ASN1_TAG_RESERVED_15]       = */ 0,
    /* [ASN1_TAG_SEQUENCE]          = */ 0,
    /* [ASN1_TAG_SET]               = */ 0,
    /* [ASN1_TAG_NUMERIC_STRING]    = */ 1,
    /* [ASN1_TAG_PRINTABLE_STRING]  = */ 1,
    /* [ASN1_TAG_T61_STRING]        = */ 1,
    /* [ASN1_TAG_VIDEOTEX_STRING]   = */ 1,
    /* [ASN1_TAG_IA5_STRING]        = */ 1,
    /* [ASN1_TAG_UTC_TIME]          = */ 0,
    /* [ASN1_TAG_GENERALIZED_TIME]  = */ 0,
    /* [ASN1_TAG_GRAPHIC_STRING]    = */ 1,
    /* [ASN1_TAG_VISIBLE_STRING]    = */ 1,
    /* [ASN1_TAG_GENERAL_STRING]    = */ 1,
    /* [ASN1_TAG_UNIVERSAL_STRING]  = */ 4,
    /* [ASN1_TAG_CHARACTER_STRING]  = */ 1,
    /* [ASN1_TAG_BMP_STRING]        = */ 2,
};




/*
 * ISO/IEC-2022 + TeletexString mess.
 */

/**
 * ISO-2022 codepoint mappings.
 */
typedef struct RTISO2022MAP
{
    /** The number of bytes per character. */
    uint8_t         cb;
    /** The registration number. */
    uint16_t        uRegistration;
    /** The size of the pauToUni table. */
    uint16_t        cToUni;
    /** Pointer to the convertion table from ISO-2022 to Unicode.
     * ASSUMES that unicode chars above 0xffff won't be required.  */
    uint16_t const *pauToUni;

    /** Escape sequence for loading into G0 or C0 or C1 depending on the type (sans
     * ESC). */
    uint8_t         abEscLoadXX[6];
    /** Escape sequence for loading into G1 (sans ESC). */
    uint8_t         abEscLoadG1[6];
    /** Escape sequence for loading into G2 (sans ESC). */
    uint8_t         abEscLoadG2[6];
    /** Escape sequence for loading into G3 (sans ESC). */
    uint8_t         abEscLoadG3[6];
} RTISO2022MAP;
/** Pointer to const ISO-2022 mappings. */
typedef RTISO2022MAP const *PCRTISO2022MAP;

/** Unused codepoint value. */
#define RTISO2022_UNUSED    UINT16_C(0xffff)


/** Dummy mappings to avoid dealing with NULL pointers in the decoder
 *  registers. */
static const RTISO2022MAP g_DummyMap =
{
    1, UINT16_MAX, 0,  NULL,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* No escape into G0 */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* No escape into G1 */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* No escape into G2 */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* No escape into G3 */
};


/** GL mappings for ISO-IR-168 (Japanese, update of #87), with space and
 *  delete. */
static const RTISO2022MAP g_IsoIr168Map =
{
    //2, 168, RT_ELEMENTS(g_awcIsoIr168Decode), g_awcIsoIr168Decode,
    2, 168, 0, NULL,
    { 0x26, 0x40, 0x2b, 0x24, 0x42, 0xff } /* Esc into G0 */,
    { 0x26, 0x40, 0x2b, 0x24, 0x29, 0x42 } /* Esc into G1 */,
    { 0x26, 0x40, 0x2b, 0x24, 0x2a, 0x42 } /* Esc into G2 */,
    { 0x26, 0x40, 0x2b, 0x24, 0x2b, 0x42 } /* Esc into G3 */,
};


/** GL mappings for ISO-IR-165 (Chinese), with space and delete. */
static const RTISO2022MAP g_IsoIr165Map =
{
    //2, 165, RT_ELEMENTS(g_awcIsoIr165Decode), g_awcIsoIr165Decode,
    2, 165, 0, NULL,
    { 0x24, 0x28, 0x45, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x24, 0x29, 0x45, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x24, 0x2a, 0x45, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x24, 0x2b, 0x45, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


/** GL mappings for ISO-IR-150 (Greek), with space and delete. */
static const RTISO2022MAP g_IsoIr150Map =
{
    //1, 150, RT_ELEMENTS(g_awcIsoIr150Decode), g_awcIsoIr150Decode,
    1, 150, 0, NULL,
    { 0x28, 0x21, 0x40, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x29, 0x21, 0x40, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x2a, 0x21, 0x40, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x2b, 0x21, 0x40, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


/** GL mappings for ISO-IR-103 (Teletex supplementary), with space and
 *  delete. */
static const RTISO2022MAP g_IsoIr103Map =
{
    //1, 103, RT_ELEMENTS(g_awcIsoIr103Decode), g_awcIsoIr103Decode,
    1, 103, 0, NULL,
    { 0x28, 0x76, 0xff, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x29, 0x76, 0xff, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x2a, 0x76, 0xff, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x2b, 0x76, 0xff, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


/**
 * GL mapping from ISO-IR-102 (Teletex primary) to unicode, with space and
 * delete.
 *
 * Mostly 1:1, except that (a) what would be dollar is currency sign, (b)
 * positions 0x5c, 0x5e, 0x7b, 0x7d and 0x7e are defined not to be used.
 */
static uint16_t const g_awcIsoIr102Decode[0x60] =
{
    0x0020, 0x0021, 0x0022, 0x0023, 0x00A4, 0x0025, 0x0026, 0x0027,  0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,  0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,  0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,  0x0058, 0x0059, 0x005a, 0x005b, 0xffff, 0x005d, 0xffff, 0x005f,
    0xffff, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,  0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,  0x0078, 0x0079, 0x007a, 0xffff, 0x007c, 0xffff, 0xffff, 0x007f,
};

/** GL mappings for ISO-IR-102, with space and delete. */
static const RTISO2022MAP g_IsoIr102Map =
{
    1, 102, RT_ELEMENTS(g_awcIsoIr102Decode), g_awcIsoIr102Decode,
    { 0x28, 0x75, 0xff, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x29, 0x75, 0xff, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x2a, 0x75, 0xff, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x2b, 0x75, 0xff, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


#if 0 /* unused */
/** GL mappings for ISO-IR-87 (Japanese),  with space and delete. */
static const RTISO2022MAP g_IsoIr87Map =
{
    //1, 87, RT_ELEMENTS(g_awcIsoIr87Decode), g_awcIsoIr97Decode,
    1, 87, 0, NULL,
    { 0x24, 0x42, 0xff, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x24, 0x29, 0x42, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x24, 0x2a, 0x42, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x24, 0x2b, 0x42, 0xff, 0xff, 0xff } /* Esc into G3 */,
};
#endif


/**
 * GL mapping from ISO-IR-6 (ASCII) to unicode, with space and delete.
 *
 * Completely 1:1.
 */
static uint16_t const g_awcIsoIr6Decode[0x60] =
{
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,  0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,  0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,  0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,  0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,  0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,  0x0078, 0x0079, 0x007a, 0x006b, 0x007c, 0x007d, 0x007e, 0x007f,
};

/** GL mappings for ISO-IR-6 (ASCII), with space and delete. */
static const RTISO2022MAP g_IsoIr6Map =
{
    1, 6, RT_ELEMENTS(g_awcIsoIr6Decode), g_awcIsoIr6Decode,
    { 0x28, 0x42, 0xff, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x29, 0x42, 0xff, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


/** GL maps. */
static PCRTISO2022MAP g_paGLMaps[] =
{
    &g_IsoIr6Map,
    &g_IsoIr102Map,
    &g_IsoIr103Map,
    &g_IsoIr150Map,
    &g_IsoIr165Map,
    &g_IsoIr168Map,
};



/** GR mappings for ISO-IR-164 (Hebrew supplementary). */
static const RTISO2022MAP g_IsoIr164Map =
{
    //1, 164, RT_ELEMENTS(g_awcIsoIr164Decode), g_awcIsoIr164Decode
    1, 164, 0, NULL,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x2d, 0x53, 0xff, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x2e, 0x53, 0xff, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x2f, 0x53, 0xff, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


/** GR mappings for ISO-IR-156 (Supplementary for ASCII (#6)). */
static const RTISO2022MAP g_IsoIr156Map =
{
    //1, 156, RT_ELEMENTS(g_awcIsoIr156Decode), g_awcIsoIr156Decode
    1, 156, 0, NULL,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x2d, 0x52, 0xff, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x2e, 0x52, 0xff, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x2f, 0x52, 0xff, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


/** GR mappings for ISO-IR-153 (Basic Cyrillic). */
static const RTISO2022MAP g_IsoIr153Map =
{
    //1, 153, RT_ELEMENTS(g_awcIsoIr153Decode), g_awcIsoIr153Decode
    1, 153, 0, NULL,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x2d, 0x4f, 0xff, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x2e, 0x4f, 0xff, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x2f, 0x4f, 0xff, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


/** GR mappings for ISO-IR-144 (Cryllic part of Latin/Cyrillic). */
static const RTISO2022MAP g_IsoIr144Map =
{
    //1, 144, RT_ELEMENTS(g_awcIsoIr144Decode), g_awcIsoIr144Decode
    1, 144, 0, NULL,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x2d, 0x4f, 0xff, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x2e, 0x4f, 0xff, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x2f, 0x4f, 0xff, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


/** GR mappings for ISO-IR-126 (Latin/Greek). */
static const RTISO2022MAP g_IsoIr126Map =
{
    //1, 126, RT_ELEMENTS(g_awcIsoIr126Decode), g_awcIsoIr126Decode
    1, 126, 0, NULL,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* Esc into G0 */,
    { 0x2d, 0x46, 0xff, 0xff, 0xff, 0xff } /* Esc into G1 */,
    { 0x2e, 0x46, 0xff, 0xff, 0xff, 0xff } /* Esc into G2 */,
    { 0x2f, 0x46, 0xff, 0xff, 0xff, 0xff } /* Esc into G3 */,
};


/** GR maps. */
static PCRTISO2022MAP g_paGRMaps[] =
{
    &g_IsoIr126Map,
    &g_IsoIr144Map,
    &g_IsoIr153Map,
    &g_IsoIr156Map,
    &g_IsoIr164Map,
};



/** C0 mapping from ISO-IR-106 to unicode. */
static uint16_t g_awcIsoIr106Decode[0x20] =
{
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,  0x0008, 0xffff, 0x000a, 0xffff, 0x000c, 0x000d, 0x000e, 0x000f,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,  0xffff, 0x008e, 0x000a, 0x001b, 0xffff, 0x008f, 0xffff, 0xffff,
};

/** C0 mappings for ISO-IR-106. */
static const RTISO2022MAP g_IsoIr106Map =
{
    1, 106, RT_ELEMENTS(g_awcIsoIr106Decode), g_awcIsoIr106Decode,
    { 0x21, 0x45, 0xff, 0xff, 0xff, 0xff } /* Esc into C0 */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* N/A */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* N/A */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* N/A */,
};

/** C0 maps. */
static PCRTISO2022MAP g_paC0Maps[] =
{
    &g_IsoIr106Map,
};



/** C1 mapping from ISO-IR-107 to unicode. */
static uint16_t g_awcIsoIr107Decode[0x20] =
{
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,  0xffff, 0xffff, 0xffff, 0x008b, 0x008c, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,  0xffff, 0xffff, 0xffff, 0x009b, 0xffff, 0xffff, 0xffff, 0xffff,
};

/** C1 mappings for ISO-IR-107. */
static const RTISO2022MAP g_IsoIr107Map =
{
    1, 107, RT_ELEMENTS(g_awcIsoIr107Decode), g_awcIsoIr107Decode,
    { 0x22, 0x48, 0xff, 0xff, 0xff, 0xff } /* Esc into C1 */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* N/A */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* N/A */,
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /* N/A */,
};

/** C1 maps. */
static PCRTISO2022MAP g_paC1Maps[] =
{
    &g_IsoIr107Map,
};


static int rtIso2022Decoder_LookupAndSet(PCRTISO2022MAP *ppMapRet, uint16_t uRegistration, PCRTISO2022MAP *papMaps, uint32_t cMaps)
{
    uint32_t i = cMaps;
    while (i-- > 0)
        if (papMaps[i]->uRegistration == uRegistration)
        {
            /** @todo skip non-Teletex codesets if we ever add more than we need for it. */
            *ppMapRet = papMaps[i];
            return VINF_SUCCESS;
        }
    return VERR_ASN1_INVALID_T61_STRING_ENCODING;
}


/**
 * ISO-2022 decoder state.
 */
typedef struct RTISO2022DECODERSTATE
{
    /** Pointer to the string */
    uint8_t const          *pabString;
    /** The string size. */
    uint32_t                cbString;
    /** The current string position. */
    uint32_t                offString;

    /** The GL mapping. */
    PCRTISO2022MAP          pMapGL;
    /** The GR mapping. */
    PCRTISO2022MAP          pMapGR;
    /** The lower control set (C0) mapping. */
    PCRTISO2022MAP          pMapC0;
    /** The higher control set (C1) mapping. */
    PCRTISO2022MAP          pMapC1;
    /** The G0, G1, G2, and G3 mappings. */
    PCRTISO2022MAP          apMapGn[4];
    /** Used by SS2 & SS3 to store the orignal GL value that is to be restored. */
    PCRTISO2022MAP          pRestoreGL;
    /** Pointer to extended error info buffer, optional. */
    PRTERRINFO              pErrInfo;
} RTISO2022DECODERSTATE;
/** Pointer to a const ISO-2022 decoder state. */
typedef RTISO2022DECODERSTATE *PRTISO2022DECODERSTATE;


static int rtIso2022Decoder_SetGL(PRTISO2022DECODERSTATE pThis, PCRTISO2022MAP pNewMap)
{
    pThis->pMapGL = pNewMap;
    return VINF_SUCCESS;
}


static int rtIso2022Decoder_SetGR(PRTISO2022DECODERSTATE pThis, PCRTISO2022MAP pNewMap)
{
    pThis->pMapGR = pNewMap;
    return VINF_SUCCESS;
}


static int rtIso2022Decoder_SetGLForOneChar(PRTISO2022DECODERSTATE pThis, PCRTISO2022MAP pTmpMap)
{
    pThis->pRestoreGL = pThis->pMapGL;
    pThis->pMapGL     = pTmpMap;
    return VINF_SUCCESS;
}


static int rtIso2022Decoder_SetC0(PRTISO2022DECODERSTATE pThis, uint16_t uRegistration)
{
    return rtIso2022Decoder_LookupAndSet(&pThis->pMapC0, uRegistration, g_paC0Maps, RT_ELEMENTS(g_paC0Maps));
}


static int rtIso2022Decoder_SetC1(PRTISO2022DECODERSTATE pThis, uint16_t uRegistration)
{
    return rtIso2022Decoder_LookupAndSet(&pThis->pMapC1, uRegistration, g_paC1Maps, RT_ELEMENTS(g_paC1Maps));
}


/**
 * Worker for rtIso2022Decoder_FindEscAndSet.
 *
 * @returns true if match, false if not.
 * @param   pabLeft     Pointer to the first string byte after the ESC.
 * @param   cbLeft      The number of bytes left in the string.
 * @param   pabRight    Pointer to the abEscLoad* byte array to match with.
 * @param   cbRight     Size of the mapping sequence (fixed).
 * @param   pcchMatch   Where to return the length of the escape sequence (sans
 *                      ESC) on success.
 */
static bool rtIso2022Decoder_MatchEscSeqFrom2ndByte(uint8_t const *pabLeft, uint32_t cbLeft,
                                                   uint8_t const *pabRight, uint32_t cbRight,
                                                   uint32_t *pcchMatch)
{
    Assert(cbRight == 6);
    uint32_t i = 1;
    while (i < cbRight)
    {
        if (pabRight[i] == 0xff)
            break;
        if (cbLeft <= i || pabLeft[i] != pabRight[i])
            return false;
        i++;
    }
    *pcchMatch = i;
    return true;
}


/**
 * Finds a the set with a matching abEscLoad* escape sequence and loads it into
 * the designated register.
 *
 * @returns The length of the sequence on success, negative error status code on
 *          failure.
 * @param   pThis               The decoder instance.
 * @param   ppMapRet            Used to specify C0 or C1 maps when processing
 *                              escape sequences for loading these.  Only the
 *                              abEscLoadXX arrays will be searched if this is
 *                              not NULL.  For loading {G0,...,G3} pass NULL.
 * @param   pb                  Pointer to the start of the escape sequence.
 * @param   cb                  The number of bytes remaining in the string.
 * @param   papMaps             The maps to search.
 * @param   cMaps               The number of maps @a papMaps points to.
 */
static int rtIso2022Decoder_FindEscAndSet(PRTISO2022DECODERSTATE pThis,
                                          PCRTISO2022MAP *ppMapRet, PCRTISO2022MAP *papMaps, uint32_t cMaps)
{
    /* Skip the ESC.*/
    uint8_t const  *pb = &pThis->pabString[pThis->offString + 1];
    uint32_t        cb = pThis->cbString - (pThis->offString + 1);

    /* Cache the first char. */
    uint8_t const b0 = pb[0];

    /* Scan the array of maps for matching sequences. */
    uint32_t i = cMaps;
    while (i-- > 0)
    {
        uint32_t cchMatch = 0; /* (MSC maybe used uninitialized) */
        PCRTISO2022MAP pMap = papMaps[i];
        /** @todo skip non-Teletex codesets if we ever add more than we need for it. */
        if (   pMap->abEscLoadXX[0] == b0
            && rtIso2022Decoder_MatchEscSeqFrom2ndByte(pb, cb, pMap->abEscLoadXX, sizeof(pMap->abEscLoadXX), &cchMatch) )
        {
            if (ppMapRet)
                *ppMapRet         = pMap;
            else
                pThis->apMapGn[0] = pMap;
            return cchMatch + 1;
        }

        if (!ppMapRet) /* ppMapRet is NULL if Gn. */
        {
            uint32_t iGn;
            if (   pMap->abEscLoadG1[0] == b0
                && rtIso2022Decoder_MatchEscSeqFrom2ndByte(pb, cb, pMap->abEscLoadG1, sizeof(pMap->abEscLoadG1), &cchMatch))
                iGn = 1;
            else if (   pMap->abEscLoadG2[0] == b0
                     && rtIso2022Decoder_MatchEscSeqFrom2ndByte(pb, cb, pMap->abEscLoadG2, sizeof(pMap->abEscLoadG2), &cchMatch))
                iGn = 2;
            else if (   pMap->abEscLoadG3[0] == b0
                     && rtIso2022Decoder_MatchEscSeqFrom2ndByte(pb, cb, pMap->abEscLoadG3, sizeof(pMap->abEscLoadG3), &cchMatch))
                iGn = 3;
            else
                iGn = UINT32_MAX;
            if (iGn != UINT32_MAX)
            {
                pThis->apMapGn[iGn] = pMap;
                return cchMatch + 1;
            }
        }
    }
    return VERR_ASN1_TELETEX_UNSUPPORTED_CHARSET;
}


/**
 * Interprets an escape sequence.
 *
 * @returns The length of the sequence on success, negative error status code on
 *          failure.
 * @param   pThis               The decoder instance.  The offString must be
 *                              pointing to the escape byte.
 */
static int rtIso2022Decoder_InterpretEsc(PRTISO2022DECODERSTATE pThis)
{
    /* the first escape byte. */
    uint32_t offString = pThis->offString;
    if (offString + 1 >= pThis->cbString)
        return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                             "@%u: Unexpected EOS parsing ESC...", offString);
    int rc;
    switch (pThis->pabString[offString + 1])
    {
        /*
         * GL selection:
         */
        case 0x6e: /* Lock shift two: G2 -> GL */
            rc = rtIso2022Decoder_SetGL(pThis, pThis->apMapGn[2]);
            break;
        case 0x6f: /* Lock shift three: G3 -> GL */
            rc = rtIso2022Decoder_SetGL(pThis, pThis->apMapGn[3]);
            break;
        case 0x4e:  /* Single shift two:  G2 -> GL for one char. */
            rc = rtIso2022Decoder_SetGLForOneChar(pThis, pThis->apMapGn[2]);
            break;
        case 0x4f: /* Single shift three: G3 -> GL for one char. */
            rc = rtIso2022Decoder_SetGLForOneChar(pThis, pThis->apMapGn[3]);
            break;

        /*
         * GR selection:
         */
        case 0x7e: /* Locking shift one right:   G1 -> GR. */
            rc = rtIso2022Decoder_SetGR(pThis, pThis->apMapGn[1]);
            break;
        case 0x7d: /* Locking shift two right:   G2 -> GR. */
            rc = rtIso2022Decoder_SetGR(pThis, pThis->apMapGn[2]);
            break;
        case 0x7c: /* Locking shift three right: G3 -> GR. */
            rc = rtIso2022Decoder_SetGR(pThis, pThis->apMapGn[3]);
            break;

        /*
         * Cx selection:
         */
        case 0x21: /* C0-designate */
            return rtIso2022Decoder_FindEscAndSet(pThis, &pThis->pMapC0, g_paC0Maps, RT_ELEMENTS(g_paC0Maps));
        case 0x22: /* C1-designate */
            return rtIso2022Decoder_FindEscAndSet(pThis, &pThis->pMapC1, g_paC1Maps, RT_ELEMENTS(g_paC1Maps));

        /*
         * Single-byte character set selection.
         */
        case 0x28: /* G0-designate, 94 chars. */
        case 0x29: /* G1-designate, 94 chars. */
        case 0x2a: /* G2-designate, 94 chars. */
        case 0x2b: /* G3-designate, 94 chars. */
            return rtIso2022Decoder_FindEscAndSet(pThis, NULL, g_paGLMaps, RT_ELEMENTS(g_paGLMaps));

        case 0x2c: /* G0-designate, 96 chars. */
        case 0x2d: /* G1-designate, 96 chars. */
        case 0x2e: /* G2-designate, 96 chars. */
        case 0x2f: /* G3-designate, 96 chars. */
            return rtIso2022Decoder_FindEscAndSet(pThis, NULL, g_paGRMaps, RT_ELEMENTS(g_paGRMaps));

        /*
         * Multibyte character set selection.
         */
        case 0x24:
            if (offString + 2 >= pThis->cbString)
                return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                     "@%u: Unexpected EOS parsing ESC %#x...", offString, pThis->pabString[offString + 1]);
            switch (pThis->pabString[offString + 2])
            {
                case 0x28: /* G0-designate, 94^n chars. */
                case 0x29: /* G1-designate, 94^n chars. */
                case 0x2a: /* G2-designate, 94^n chars. */
                case 0x2b: /* G3-designate, 94^n chars. */
                default:   /* G0-designate that skips the 0x28? (See japanese ones.) */
                    return rtIso2022Decoder_FindEscAndSet(pThis, NULL, g_paGLMaps, RT_ELEMENTS(g_paGLMaps));

                case 0x2c: /* G0-designate, 96^n chars. */
                case 0x2d: /* G1-designate, 96^n chars. */
                case 0x2e: /* G2-designate, 96^n chars. */
                case 0x2f: /* G3-designate, 96^n chars. */
                    return rtIso2022Decoder_FindEscAndSet(pThis, NULL, g_paGRMaps, RT_ELEMENTS(g_paGRMaps));
            }                                            \
            break;

        case 0x26: /* Special escape prefix for #168. */
            return rtIso2022Decoder_FindEscAndSet(pThis, NULL, g_paGLMaps, RT_ELEMENTS(g_paGLMaps));

        /*
         * Unknown/unsupported/unimplemented.
         */
        case 0x25: /* Designate other coding system. */
            return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_TELETEX_UNSUPPORTED_ESC_SEQ,
                                 "@%u: ESC DOCS not supported\n", offString);
        default:
            return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_TELETEX_UNKNOWN_ESC_SEQ,
                                 "@%u: Unknown escape sequence: ESC %#x...\n", offString, pThis->pabString[offString + 1]);
    }

    /* Only single byte escapes sequences for shifting ends up here. */
    if (RT_SUCCESS(rc))
        return 1;
    return rc;
}


static int rtIso2022Decoder_ControlCharHook(PRTISO2022DECODERSTATE pThis, uint16_t wcControl)
{
    int rc;
    switch (wcControl)
    {
        case 0x000e: /* Locking shift zero: G0 -> GL. */
            rc = rtIso2022Decoder_SetGL(pThis, pThis->apMapGn[0]);
            break;

        case 0x000f: /* Locking shift one:  G1 -> GL. */
            rc = rtIso2022Decoder_SetGL(pThis, pThis->apMapGn[1]);
            break;

        case 0x008e:  /* Single shift two:  G2 -> GL for one char. */
            rc = rtIso2022Decoder_SetGLForOneChar(pThis, pThis->apMapGn[2]);
            break;

        case 0x008f: /* Single shift three: G3 -> GL for one char. */
            rc = rtIso2022Decoder_SetGLForOneChar(pThis, pThis->apMapGn[3]);
            break;

        case 0x002b: /* Escape should be handled by the caller. */
            rc = rtIso2022Decoder_InterpretEsc(pThis);
            break;

        default:
            return 0;
    }

    return RT_SUCCESS(rc) ? 1 : rc;
}


static int rtIso2022Decoder_Init(PRTISO2022DECODERSTATE pThis, const char *pchString, uint32_t cchString,
                                 uint32_t uGL, uint32_t uC0, uint32_t uC1, uint32_t uG0,
                                 PRTERRINFO pErrInfo)
{
    pThis->pabString  = (uint8_t const *)pchString;
    pThis->cbString   = cchString;
    pThis->offString  = 0;

    pThis->pMapGL     = &g_DummyMap;
    pThis->pMapGR     = &g_DummyMap;
    pThis->pMapC0     = &g_DummyMap;
    pThis->pMapC1     = &g_DummyMap;
    pThis->pRestoreGL = NULL;
    pThis->apMapGn[0] = &g_DummyMap;
    pThis->apMapGn[1] = &g_DummyMap;
    pThis->apMapGn[2] = &g_DummyMap;
    pThis->apMapGn[3] = &g_DummyMap;
    pThis->pErrInfo   = pErrInfo;

    int rc = VINF_SUCCESS;
    if (uGL != UINT32_MAX)
        rc = rtIso2022Decoder_LookupAndSet(&pThis->pMapGL,     uGL, g_paGLMaps, RT_ELEMENTS(g_paGLMaps));
    if (RT_SUCCESS(rc) && uG0 != UINT32_MAX)
        rc = rtIso2022Decoder_LookupAndSet(&pThis->apMapGn[0], uG0, g_paGLMaps, RT_ELEMENTS(g_paGLMaps));
    if (RT_SUCCESS(rc) && uC0 != UINT32_MAX)
        rc = rtIso2022Decoder_SetC0(pThis, uC0);
    if (RT_SUCCESS(rc) && uC1 != UINT32_MAX)
        rc = rtIso2022Decoder_SetC1(pThis, uC1);
    return rc;
}


static int rtIso2022Decoder_GetNextUniCpSlow(PRTISO2022DECODERSTATE pThis, PRTUNICP pUniCp)
{
    while (pThis->offString < pThis->cbString)
    {
        uint8_t  b = pThis->pabString[pThis->offString];
        if (!(b & 0x80))
        {
            if (b >= 0x20)
            {
                /*
                 * GL range.
                 */
                b -= 0x20;
                PCRTISO2022MAP pMap = pThis->pMapGL;

                /* Single byte character map. */
                if (pMap->cb == 1)
                {
                    if (RT_LIKELY(b < pMap->cToUni))
                    {
                        uint16_t wc = pMap->pauToUni[b];
                        if (RT_LIKELY(wc != RTISO2022_UNUSED))
                        {
                            *pUniCp = wc;
                            pThis->offString += 1;
                            return VINF_SUCCESS;
                        }
                        *pUniCp = RTUNICP_INVALID;
                        return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                             "@%u: GL b=%#x is marked unused in map #%u range %u.",
                                             pThis->offString, b + 0x20, pMap->uRegistration, pMap->cToUni);
                    }
                    *pUniCp = RTUNICP_INVALID;
                    return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                         "@%u: GL b=%#x is outside map #%u range %u.",
                                         pThis->offString, b + 0x20, pMap->uRegistration, pMap->cToUni);
                }

                /* Double byte character set. */
                Assert(pMap->cb == 2);
                if (pThis->offString + 1 < pThis->cbString)
                {
                    uint8_t b2 = pThis->pabString[pThis->offString + 1];
                    b2 -= 0x20;
                    if (RT_LIKELY(b2 < 0x60))
                    {
                        uint16_t u16 = ((uint16_t)b << 8) | b2;
                        if (RT_LIKELY(u16 < pMap->cToUni))
                        {
                            uint16_t wc = pMap->pauToUni[b];
                            if (RT_LIKELY(wc != RTISO2022_UNUSED))
                            {
                                *pUniCp = wc;
                                pThis->offString += 2;
                                return VINF_SUCCESS;
                            }
                            *pUniCp = RTUNICP_INVALID;
                            return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                                 "@%u: GL b=%#x is marked unused in map #%u.",
                                                 pThis->offString, b + 0x20, pMap->uRegistration);
                        }
                        if (u16 >= 0x7f00)
                        {
                            *pUniCp = 0x7f; /* delete */
                            pThis->offString += 2;
                            return VINF_SUCCESS;
                        }
                        *pUniCp = RTUNICP_INVALID;
                        return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                             "@%u: GL u16=%#x (b0=%#x b1=%#x) is outside map #%u range %u.",
                                             pThis->offString, u16, b + 0x20, b2 + 0x20, pMap->uRegistration, pMap->cToUni);
                    }
                    return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                         "@%u: 2nd GL byte outside GL range: b0=%#x b1=%#x (map #%u)",
                                         pThis->offString, b + 0x20, b2 + 0x20, pMap->uRegistration);

                }
                return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                     "@%u: EOS reading 2nd byte for GL b=%#x (map #%u).",
                                     pThis->offString, b + 0x20, pMap->uRegistration);
            }
            else
            {
                /*
                 * C0 range.
                 */
                Assert(pThis->pMapC0->cb == 0x20);
                uint16_t wc = pThis->pMapC0->pauToUni[b];
                if (wc != RTISO2022_UNUSED)
                {
                    int rc;
                    if (b == 0x1b || wc == 0x1b) /* ESC is hardcoded, or so they say. */
                        rc = rtIso2022Decoder_InterpretEsc(pThis);
                    else
                        rc = rtIso2022Decoder_ControlCharHook(pThis, wc);
                    if (RT_SUCCESS(rc))
                    {
                        if (rc == 0)
                        {
                            pThis->offString += 1;
                            *pUniCp = wc;
                            return VINF_SUCCESS;
                        }
                        pThis->offString += rc;
                    }
                    else
                        return rc;
                }
                else
                    return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                         "@%u: C0 b=%#x is marked unused in map #%u.",
                                         pThis->offString, b, pThis->pMapC0->uRegistration);
            }
        }
        else
        {
            if (b >= 0xa0)
            {
                /*
                 * GR range.
                 */
                b -= 0xa0;
                PCRTISO2022MAP pMap = pThis->pMapGR;

                /* Single byte character map. */
                if (pMap->cb == 1)
                {
                    /** @todo 0xa0 = SPACE and 0xff = DELETE if it's a 94 charater map... */
                    if (RT_LIKELY(b < pMap->cToUni))
                    {
                        uint16_t wc = pMap->pauToUni[b];
                        if (RT_LIKELY(wc != RTISO2022_UNUSED))
                        {
                            *pUniCp = wc;
                            pThis->offString += 1;
                            return VINF_SUCCESS;
                        }

                        *pUniCp = RTUNICP_INVALID;
                        return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                             "@%u: GR b=%#x is marked unused in map #%u.",
                                             pThis->offString, b + 0xa0, pMap->uRegistration);
                    }
                    *pUniCp = RTUNICP_INVALID;
                    return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                         "@%u: GR b=%#x is outside map #%u range %u",
                                         pThis->offString, b + 0xa0, pMap->uRegistration, pMap->cToUni);
                }

                /* Double byte character set. */
                Assert(pMap->cb == 2);
                if (pThis->offString + 1 < pThis->cbString)
                {
                    uint8_t b2 = pThis->pabString[pThis->offString + 1];
                    b2 -= 0xa0;
                    if (RT_LIKELY(b2 < 0x60))
                    {
                        uint16_t u16 = ((uint16_t)b << 8) | b2;
                        if (RT_LIKELY(u16 < pMap->cToUni))
                        {
                            uint16_t wc = pMap->pauToUni[b];
                            if (RT_LIKELY(wc != RTISO2022_UNUSED))
                            {
                                *pUniCp = wc;
                                pThis->offString += 2;
                                return VINF_SUCCESS;
                            }

                            *pUniCp = RTUNICP_INVALID;
                            return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                                 "@%u: GR b=%#x is marked unused in map #%u.",
                                                 pThis->offString, b + 0xa0, pMap->uRegistration);
                        }
                        *pUniCp = RTUNICP_INVALID;
                        return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                             "@%u: GR u16=%#x (b0=%#x b1=%#x) is outside map #%u range %u.",
                                             pThis->offString, u16, b + 0xa0, b2 + 0xa0, pMap->uRegistration, pMap->cToUni);
                    }
                    return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                         "@%u: 2nd GR byte outside GR range: b0=%#x b1=%#x (map #%u).",
                                         pThis->offString, b + 0xa0, b2 + 0xa0, pMap->uRegistration);

                }
                return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                     "@%u: EOS reading 2nd byte for GR b=%#x (map #%u).",
                                     pThis->offString, b + 0xa0, pMap->uRegistration);
            }
            else
            {
                /*
                 * C2 range.
                 */
                Assert(pThis->pMapC1->cb == 0x20);
                b -= 0x80;
                uint16_t wc = pThis->pMapC1->pauToUni[b];
                if (wc != RTISO2022_UNUSED)
                {
                    int rc = rtIso2022Decoder_ControlCharHook(pThis, wc);
                    if (RT_SUCCESS(rc))
                    {
                        if (rc == 0)
                        {
                            pThis->offString += 1;
                            *pUniCp = wc;
                            return VINF_SUCCESS;
                        }
                        pThis->offString += rc;
                    }
                    else
                        return rc;
                }
                else
                    return RTErrInfoSetF(pThis->pErrInfo, VERR_ASN1_INVALID_T61_STRING_ENCODING,
                                         "@%u: C1 b=%#x is marked unused in map #%u.",
                                         pThis->offString, b + 0x80, pThis->pMapC1->uRegistration);
            }
        }
    }

    /* End of string. */
    *pUniCp = RTUNICP_INVALID;
    return VERR_END_OF_STRING;
}

DECLINLINE(int) rtIso2022Decoder_GetNextUniCp(PRTISO2022DECODERSTATE pThis, PRTUNICP pUniCp)
{
    /*
     * Deal with single byte GL.
     */
    uint32_t const offString = pThis->offString;
    if (pThis->offString < pThis->cbString)
    {
        PCRTISO2022MAP const pMapGL = pThis->pMapGL;
        if (pMapGL->cb == 1)
        {
            uint8_t const b = pThis->pabString[offString] - (uint8_t)0x20;
            if (b < pMapGL->cToUni)
            {
                uint16_t wc = pMapGL->pauToUni[b];
                if (wc != RTISO2022_UNUSED)
                {
                    pThis->offString = offString + 1;
                    *pUniCp = wc;
                    return VINF_SUCCESS;
                }
            }
        }

        /*
         * Deal with complications in the non-inline function.
         */
        return rtIso2022Decoder_GetNextUniCpSlow(pThis, pUniCp);
    }

    *pUniCp = RTUNICP_INVALID;
    return VERR_END_OF_STRING;
}


static int rtIso2022ValidateString(uint32_t uProfile, const char *pch, uint32_t cch, size_t *pcchUtf8, PRTERRINFO pErrInfo)
{
    AssertReturn(uProfile == ASN1_TAG_T61_STRING, VERR_INVALID_PARAMETER); /* just a place holder for now. */

    RTISO2022DECODERSTATE Decoder;
    int rc = rtIso2022Decoder_Init(&Decoder, pch, cch, 102, 106, 107, 102, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        size_t cchUtf8 = 0;
        for (;;)
        {
            RTUNICP uc;
            rc = rtIso2022Decoder_GetNextUniCp(&Decoder, &uc);
            if (RT_SUCCESS(rc))
                cchUtf8 += RTStrCpSize(uc);
            else
            {
                if (RT_LIKELY(rc == VERR_END_OF_STRING))
                {
                    *pcchUtf8 = cchUtf8;
                    return VINF_SUCCESS;
                }
                return rc;
            }
        }
    }
    return rc;
}


static int rtIso2022RecodeAsUtf8(uint32_t uProfile, const char *pchSrc, uint32_t cchSrc, char *pszDst, size_t cbDst)
{
    AssertReturn(uProfile == ASN1_TAG_T61_STRING, VERR_INVALID_PARAMETER); /* just a place holder for now. */
    AssertReturn(cbDst > 0, VERR_INVALID_PARAMETER);

    RTISO2022DECODERSTATE Decoder;
    int rc = rtIso2022Decoder_Init(&Decoder, pchSrc, cchSrc, 102, 106, 107, 102, NULL /*pErrInfo*/);
    if (RT_SUCCESS(rc))
    {
        for (;;)
        {
            RTUNICP uc;
            rc = rtIso2022Decoder_GetNextUniCp(&Decoder, &uc);
            if (RT_SUCCESS(rc))
            {
                if (uc < 0x80 && cbDst > 1)
                {
                    *pszDst++ = (char)uc;
                    cbDst--;
                }
                else
                {
                    size_t cchUniCp = RTStrCpSize(uc);
                    if (cbDst > cchUniCp)
                    {
                        cbDst -= cchUniCp;
                        pszDst = RTStrPutCp(pszDst, uc);
                    }
                    else
                    {
                        *pszDst = '\0';
                        return VERR_BUFFER_OVERFLOW;
                    }
                }
            }
            else if (RT_LIKELY(rc == VERR_END_OF_STRING))
            {
                *pszDst = '\0';
                return VINF_SUCCESS;
            }
            else
                return rc;
        }
    }
    return rc;
}



/** The unicode mapping of the C1 area of windows codepage 1252.
 * The rest of the code page is 1:1 with unicode.   */
static uint16_t g_awcWin1252_C1[0x20] =
{
    0x20ac, 0x0081, 0x201a, 0x0192,  0x201e, 0x2026, 0x2020, 0x2021,  0x02c6, 0x2030, 0x0160, 0x2039,  0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c,  0x201d, 0x2022, 0x2013, 0x2014,  0x02dc, 0x2122, 0x0161, 0x203a,  0x0153, 0x009d, 0x017e, 0x0178,
};


static size_t rtWin1252CalcUtf8Length(const char *pch, uint32_t cch)
{
    size_t cchUtf8 = 0;
    while (cch-- > 0)
    {
        uint8_t const b = *pch++;
        if (b < 0x80)
            cchUtf8 += 1;
        else if (b >= 0xa0)
            cchUtf8 += 2;
        else
        {
            uint16_t const wc = g_awcWin1252_C1[b - 0x80];
            cchUtf8 += RTStrCpSize(wc);
        }
    }
    return cchUtf8;
}


static int rtWin1252RecodeAsUtf8(const char *pchSrc, uint32_t cchSrc, char *pszDst, size_t cbDst)
{
    while (cchSrc-- > 0)
    {
        uint8_t b = *pchSrc++;
        if (b < 0x80)
        {
            if (cbDst <= 1)
                return VERR_BUFFER_OVERFLOW;
            *pszDst++ = (char)b;
        }
        else
        {
            uint16_t const wc = b >= 0xa0 ? b : g_awcWin1252_C1[b - 0x80];
            size_t         cchCp = RTStrCpSize(wc);
            if (cbDst <= cchCp)
                return VERR_BUFFER_OVERFLOW;
            pszDst = RTStrPutCp(pszDst, wc);
        }
    }

    if (!cbDst)
        return VERR_BUFFER_OVERFLOW;
    *pszDst = '\0';
    return VINF_SUCCESS;
}



/*
 * ASN.1 STRING - Specific Methods.
 */

/** rtAsn1String_IsTeletexLatin1 results. */
typedef enum RTASN1TELETEXVARIANT
{
    /** Couldn't find hard evidence of either. */
    RTASN1TELETEXVARIANT_UNDECIDED = 1,
    /** Pretty certain that it's real teletex. */
    RTASN1TELETEXVARIANT_TELETEX,
    /** Pretty sure it's latin-1 or Windows-1252. */
    RTASN1TELETEXVARIANT_LATIN1,
    /** Pretty sure it's Windows-1252. */
    RTASN1TELETEXVARIANT_WIN_1252
} RTASN1TELETEXVARIANT;

/**
 * Takes a guess as whether TELETEX STRING (T61 STRING) is actually is Latin-1
 * or the real thing.
 *
 * According to RFC-2459, section 4.1.2.4, various libraries, certificate
 * authorities and others have perverted the TeletexString/T61String tag by
 * ISO-8859-1 (aka latin-1) strings (more probably these are actually Windows
 * CP-1252 rather than latin-1).  We'll try detect incompatible latin-1
 * perversions by:
 *      - The use of GR (0xf0-0xff) chars.
 *      - The lack of ESC sequences and shifts (LS0,LS1,SS2,SS3)
 *
 * An ASSUMTION here is that GR is not loaded with anything at the start of a
 * teletex string, as per table 3 in section 8.23.5.2 in T-REC-X.590.200811.
 *
 * @retval  @c true if chances are good that it's LATIN-1.
 * @retval  @c false if changes are very good that it's real teletex.
 * @param   pch         The first char in the string.
 * @param   cch         The string length.
 *
 * @remarks Useful info on Teletex and ISO/IEC-2022:
 *          https://www.mail-archive.com/asn1@asn1.org/msg00460.html
 *          http://en.wikipedia.org/wiki/ISO/IEC_2022
 *          http://www.open-std.org/cen/tc304/guide/GCONCEPT.HTM
 *          http://www.ecma-international.org/publications/files/ECMA-ST/Ecma-035.pdf
 */
static RTASN1TELETEXVARIANT rtAsn1String_IsTeletexLatin1(const char *pch, uint32_t cch)
{
    RTASN1TELETEXVARIANT enmVariant = RTASN1TELETEXVARIANT_UNDECIDED;
    while (cch-- > 0)
    {
        uint8_t const b = *pch;
        if (b >= 0x20 && b <= 0x7f)
        {
            if (g_awcIsoIr102Decode[b - 0x20] == RTISO2022_UNUSED)
                enmVariant = RTASN1TELETEXVARIANT_LATIN1;
        }
        else
        {
            if (   b == 0x1b /* ESC */
                || b == 0x0e /* LS0 / SI */
                || b == 0x0f /* LS1 / SO */
                || b == 0x19 /* SS2 */
                || b == 0x1d /* SS3 */ )
                return RTASN1TELETEXVARIANT_TELETEX;

            if (b >= 0xa0)
                enmVariant = RTASN1TELETEXVARIANT_LATIN1;
            else if (b >= 0x80 && b <= 0x9f)
            {
                /* Any use of C1 characters defined by windows cp-1252 will
                   lead us to believe it's the windows code rather than the
                   ISO/IEC standard that is being used.  (Not that it makes
                   much of a difference, because we're gonna treat it as the
                   windows codepage, anyways.) */
                if (   b != 0x81
                    && b != 0x8d
                    && b != 0x8f
                    && b != 0x90
                    && b != 0x9d)
                    return RTASN1TELETEXVARIANT_WIN_1252;
            }
        }
    }
    return RTASN1TELETEXVARIANT_UNDECIDED;
}


/**
 * Checks the encoding of an ASN.1 string according to it's tag.
 *
 * @returns IPRT status code.
 * @param   pThis       The string to santity check.
 * @param   pErrInfo    Where to store extra error info.  Optional.
 * @param   pcchUtf8    Where to return the UTF-8 string length.  Optional.
 */
static int rtAsn1String_CheckSanity(PCRTASN1STRING pThis, PRTERRINFO pErrInfo, const char *pszErrorTag, size_t *pcchUtf8)
{
    int         rc;
    uint32_t    cch     = pThis->Asn1Core.cb;
    size_t      cchUtf8 = cch;
    const char *pch     = pThis->Asn1Core.uData.pch;
    uint32_t    uTag    = RTASN1CORE_GET_TAG(&pThis->Asn1Core);
    switch (uTag)
    {
        case ASN1_TAG_UTF8_STRING:
            rc = RTStrValidateEncodingEx(pch, cch, 0);
            if (RT_SUCCESS(rc))
                break;
            return RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_UTF8_STRING_ENCODING, "%s: Bad UTF-8 encoding (%Rrc, %.*Rhxs)",
                                 pszErrorTag, rc, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);

        case ASN1_TAG_NUMERIC_STRING:
            while (cch-- > 0)
            {
                char ch = *pch++;
                if (   !RT_C_IS_DIGIT(ch)
                    && ch != ' ')
                    return RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_NUMERIC_STRING_ENCODING,
                                         "%s: Bad numeric string: ch=%#x (pos %u in %.*Rhxs)", pszErrorTag, ch,
                                         pThis->Asn1Core.cb - cch + 1, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
            }
            break;

        case ASN1_TAG_PRINTABLE_STRING:
            while (cch-- > 0)
            {
                char ch = *pch++;
                if (   !RT_C_IS_ALNUM(ch)
                    && ch != ' '
                    && ch != '\''
                    && ch != '('
                    && ch != ')'
                    && ch != '+'
                    && ch != ','
                    && ch != '-'
                    && ch != '.'
                    && ch != '/'
                    && ch != ':'
                    && ch != '='
                    && ch != '?'
                   )
                    return RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_PRINTABLE_STRING_ENCODING,
                                         "%s: Bad printable string: ch=%#x (pos %u in %.*Rhxs)", pszErrorTag, ch,
                                         pThis->Asn1Core.cb - cch + 1, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
            }
            break;

        case ASN1_TAG_IA5_STRING: /* ASCII */
            while (cch-- > 0)
            {
                unsigned char ch = *pch++;
                if (ch == 0 || ch >= 0x80)
                {
                    /* Ignore C-style zero terminator as the "Microsoft ECC Product Root Certificate Authority 2018"
                       for instance, has a policy qualifier string "http://www.microsoft.com/pkiops/Docs/Repository.htm\0" */
                    /** @todo should '\0' really be excluded above? */
                    if (ch != 0 || cch != 0)
                        return RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_IA5_STRING_ENCODING,
                                             "%s: Bad IA5 string: ch=%#x (pos %u in %.*Rhxs)", pszErrorTag, ch,
                                             pThis->Asn1Core.cb - cch + 1, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
                    break;
                }
            }
            break;

        case ASN1_TAG_T61_STRING:
            switch (rtAsn1String_IsTeletexLatin1(pch, cch))
            {
                default:
                    rc = rtIso2022ValidateString(ASN1_TAG_T61_STRING, pch, cch, &cchUtf8, pErrInfo);
                    if (RT_FAILURE(rc))
                        return rc;
                    break;
                case RTASN1TELETEXVARIANT_UNDECIDED:
                case RTASN1TELETEXVARIANT_LATIN1:
                case RTASN1TELETEXVARIANT_WIN_1252:
                    cchUtf8 = rtWin1252CalcUtf8Length(pch, cch);
                    break;
            }
            break;

        case ASN1_TAG_VIDEOTEX_STRING:
        case ASN1_TAG_GRAPHIC_STRING:
            return VERR_ASN1_STRING_TYPE_NOT_IMPLEMENTED;

        case ASN1_TAG_VISIBLE_STRING:
            while (cch-- > 0)
            {
                unsigned char ch = *pch++;
                if (ch < 0x20 || ch >= 0x7f)
                    return RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_VISIBLE_STRING_ENCODING,
                                         "%s: Bad visible string: ch=%#x (pos %u in %.*Rhxs)", pszErrorTag, ch,
                                         pThis->Asn1Core.cb - cch + 1, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
            }
            break;

        case ASN1_TAG_GENERAL_STRING:
            return VERR_ASN1_STRING_TYPE_NOT_IMPLEMENTED;

        case ASN1_TAG_UNIVERSAL_STRING:
            if (!(cch & 3))
            {
                uint8_t const *pb = (uint8_t const *)pch;
                cchUtf8 = 0;
                while (cch > 0)
                {
                    RTUNICP uc = RT_MAKE_U32_FROM_U8(pb[3], pb[2], pb[1], pb[0]); /* big endian */
                    if (!RTUniCpIsValid(uc))
                        return RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_UNIVERSAL_STRING_ENCODING,
                                             "%s: Bad universal string: uc=%#x (pos %u in %.*Rhxs)", pszErrorTag, uc,
                                             pThis->Asn1Core.cb - cch + 1, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
                    cchUtf8 += RTUniCpCalcUtf8Len(uc);

                    /* next */
                    pb  += 4;
                    cch -= 4;
                }
                break;
            }
            return RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_UNIVERSAL_STRING_ENCODING,
                                 "%s: Bad universal string: size not a multiple of 4: cch=%#x (%.*Rhxs)",
                                 pszErrorTag, cch, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);

        case ASN1_TAG_BMP_STRING:
            if (!(cch & 1))
            {
                uint8_t const *pb = (uint8_t const *)pch;
                cchUtf8 = 0;
                while (cch > 0)
                {
                    RTUNICP uc = RT_MAKE_U32_FROM_U8(pb[1], pb[0], 0, 0); /* big endian */
                    if (!RTUniCpIsValid(uc))
                        return RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_BMP_STRING_ENCODING,
                                             "%s: Bad BMP string: uc=%#x (pos %u in %.*Rhxs)", pszErrorTag, uc,
                                             pThis->Asn1Core.cb - cch + 1, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
                    cchUtf8 += RTUniCpCalcUtf8Len(uc);

                    /* next */
                    pb  += 2;
                    cch -= 2;
                }
                break;
            }
            return RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_BMP_STRING_ENCODING,
                                 "%s: Bad BMP string: odd number of bytes cch=%#x (%.*Rhxs)",
                                 pszErrorTag, cch, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);

        default:
            AssertMsgFailedReturn(("uTag=%#x\n", uTag), VERR_INTERNAL_ERROR_3);
    }

    if (pcchUtf8)
        *pcchUtf8 = cchUtf8;
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1String_CompareValues(PCRTASN1STRING pLeft, PCRTASN1STRING pRight)
{
    return RTAsn1String_CompareEx(pLeft, pRight, false /*fTypeToo*/);
}


RTDECL(int) RTAsn1String_CompareEx(PCRTASN1STRING pLeft, PCRTASN1STRING pRight, bool fTypeToo)
{
    Assert(pLeft  && (!RTAsn1String_IsPresent(pLeft)  || pLeft->Asn1Core.pOps  == &g_RTAsn1String_Vtable));
    Assert(pRight && (!RTAsn1String_IsPresent(pRight) || pRight->Asn1Core.pOps == &g_RTAsn1String_Vtable));

    int iDiff;
    if (RTAsn1String_IsPresent(pLeft))
    {
        if (RTAsn1String_IsPresent(pRight))
        {
            if (!fTypeToo || RTASN1CORE_GET_TAG(&pLeft->Asn1Core) == RTASN1CORE_GET_TAG(&pRight->Asn1Core))
                iDiff = RTAsn1Core_CompareEx(&pLeft->Asn1Core, &pRight->Asn1Core, true /*fIgnoreTagAndClass*/);
            else
                iDiff = RTASN1CORE_GET_TAG(&pLeft->Asn1Core) < RTASN1CORE_GET_TAG(&pRight->Asn1Core) ? -1 : 1;
        }
        else
            iDiff = 1;
    }
    else
        iDiff = 0 - RTAsn1String_IsPresent(pRight);
    return iDiff;
}


RTDECL(int) RTAsn1String_CompareWithString(PCRTASN1STRING pThis, const char *pszString, size_t cchString)
{
    Assert(pThis && (!RTAsn1String_IsPresent(pThis) || pThis->Asn1Core.pOps  == &g_RTAsn1String_Vtable));
    AssertPtr(pszString);

    int iDiff;
    if (RTAsn1String_IsPresent(pThis))
    {
        if (cchString == RTSTR_MAX)
            cchString = strlen(pszString);

        /*
         * If there is a UTF-8 conversion available already, use it.
         */
        if (pThis->pszUtf8)
        {
            iDiff = strncmp(pThis->pszUtf8, pszString, cchString);
            if (!iDiff && pThis->cchUtf8 != cchString)
                iDiff = pThis->cchUtf8 < cchString ? -1 : 1;
        }
        else
        {
            /*
             * Some types are UTF-8 compatible, so try do the compare without
             * RTAsn1String_QueryUtf8.
             */
            uint32_t    cch = pThis->Asn1Core.cb;
            const char *pch = pThis->Asn1Core.uData.pch;
            switch (RTASN1CORE_GET_TAG(&pThis->Asn1Core))
            {
                case ASN1_TAG_UTF8_STRING:
                case ASN1_TAG_NUMERIC_STRING:
                case ASN1_TAG_IA5_STRING:
                case ASN1_TAG_PRINTABLE_STRING:
                    iDiff = strncmp(pch, pszString, RT_MIN(cch, cchString));
                    if (iDiff && cch != cchString)
                        iDiff = cch < cchString ? - 1 : 1;
                    break;

                /** @todo Implement comparing ASN1_TAG_BMP_STRING, ASN1_TAG_UNIVERSAL_STRING and
                 *        ASN1_TAG_T61_STRING with UTF-8 strings without conversion. */

                default:
                {
                    int rc = RTAsn1String_QueryUtf8(pThis, NULL, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        iDiff = strncmp(pThis->pszUtf8, pszString, cchString);
                        if (!iDiff && pThis->cchUtf8 != cchString)
                            iDiff = pThis->cchUtf8 < cchString ? -1 : 1;
                    }
                    else
                        iDiff = -1;
                    break;
                }
            }
        }

        /* Reduce the strcmp return value. */
        if (iDiff != 0)
            iDiff = iDiff < 0 ? -1 : 1;
    }
    else
        iDiff = -1;
    return iDiff;
}


RTDECL(int) RTAsn1String_QueryUtf8(PCRTASN1STRING pThis, const char **ppsz, size_t *pcch)
{
    Assert(pThis->Asn1Core.pOps == &g_RTAsn1String_Vtable);

    char    *psz = (char *)pThis->pszUtf8;
    size_t   cch = pThis->cchUtf8;
    if (!psz)
    {

        /*
         * Convert the first time around.  Start by validating the encoding and
         * calculating the length.
         */
        int rc = rtAsn1String_CheckSanity(pThis, NULL, NULL, &cch);
        if (RT_SUCCESS(rc))
        {
            PRTASN1STRING pThisNC = (PRTASN1STRING)pThis;
            rc = RTAsn1MemAllocZ(&pThisNC->Allocation, (void **)&psz, cch + 1);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Got memory, now do the actual convertion to UTF-8 / copying.
                 */
                switch (RTASN1CORE_GET_TAG(&pThis->Asn1Core))
                {
                    case ASN1_TAG_UTF8_STRING:
                    case ASN1_TAG_NUMERIC_STRING:
                    case ASN1_TAG_PRINTABLE_STRING:
                    case ASN1_TAG_IA5_STRING:
                    case ASN1_TAG_VISIBLE_STRING:
                        Assert(cch == pThis->Asn1Core.cb);
                        memcpy(psz, pThis->Asn1Core.uData.pch, cch);
                        psz[cch] = '\0';
                        break;

                    case ASN1_TAG_T61_STRING:
                        switch (rtAsn1String_IsTeletexLatin1(pThis->Asn1Core.uData.pch, pThis->Asn1Core.cb))
                        {
                            default:
                                rc = rtIso2022RecodeAsUtf8(ASN1_TAG_T61_STRING, pThis->Asn1Core.uData.pch, pThis->Asn1Core.cb,
                                                           psz, cch + 1);
                                break;
                            case RTASN1TELETEXVARIANT_UNDECIDED:
                            case RTASN1TELETEXVARIANT_LATIN1:
                            case RTASN1TELETEXVARIANT_WIN_1252:
                                rc = rtWin1252RecodeAsUtf8(pThis->Asn1Core.uData.pch, pThis->Asn1Core.cb, psz, cch + 1);
                                break;
                        }
                        AssertReturnStmt(RT_SUCCESS(rc), RTAsn1MemFree(&pThisNC->Allocation, psz), VERR_INTERNAL_ERROR_3);
                        break;

                    /* case ASN1_TAG_VIDEOTEX_STRING: */
                    /* case ASN1_TAG_GRAPHIC_STRING:  */
                    /* case ASN1_TAG_GENERAL_STRING:  */

                    case ASN1_TAG_UNIVERSAL_STRING:
                    {
                        char           *pszDst = psz;
                        size_t          cchSrc = pThis->Asn1Core.cb;
                        uint8_t const  *pbSrc  = pThis->Asn1Core.uData.pu8;
                        while (cchSrc > 0)
                        {
                            RTUNICP uc = RT_MAKE_U32_FROM_U8(pbSrc[3], pbSrc[2], pbSrc[1], pbSrc[0]); /* big endian */
                            AssertReturnStmt(RTUniCpIsValid(uc), RTAsn1MemFree(&pThisNC->Allocation, psz), VERR_INTERNAL_ERROR_2);
                            pszDst = RTStrPutCp(pszDst, uc);

                            /* next */
                            pbSrc  += 4;
                            cchSrc -= 4;
                        }
                        Assert((size_t)(pszDst - psz) == cch);
                        break;
                    }

                    case ASN1_TAG_BMP_STRING:
                    {
                        char           *pszDst = psz;
                        size_t          cchSrc = pThis->Asn1Core.cb;
                        uint8_t const  *pbSrc  = pThis->Asn1Core.uData.pu8;
                        while (cchSrc > 0)
                        {
                            RTUNICP uc = RT_MAKE_U32_FROM_U8(pbSrc[1], pbSrc[0], 0, 0); /* big endian */
                            AssertReturnStmt(RTUniCpIsValid(uc), RTAsn1MemFree(&pThisNC->Allocation, psz), VERR_INTERNAL_ERROR_2);
                            pszDst = RTStrPutCp(pszDst, uc);

                            /* next */
                            pbSrc  += 2;
                            cchSrc -= 2;
                        }
                        Assert((size_t)(pszDst - psz) == cch);
                        break;
                    }

                    default:
                        RTAsn1MemFree(&pThisNC->Allocation, psz);
                        AssertMsgFailedReturn(("uTag=%#x\n", RTASN1CORE_GET_TAG(&pThis->Asn1Core)), VERR_INTERNAL_ERROR_3);
                }

                /*
                 * Successfully produced UTF-8.  Save it in the object.
                 */
                pThisNC->pszUtf8 = psz;
                pThisNC->cchUtf8 = (uint32_t)cch;
            }
            else
                return rc;
        }
        else
            return rc;
    }

    /*
     * Success.
     */
    if (ppsz)
        *ppsz = psz;
    if (pcch)
        *pcch = cch;
    return VINF_SUCCESS;
}



RTDECL(int) RTAsn1String_QueryUtf8Len(PCRTASN1STRING pThis, size_t *pcch)
{
    Assert(pThis->Asn1Core.pOps == &g_RTAsn1String_Vtable);

    size_t cch = pThis->cchUtf8;
    if (!cch && !pThis->pszUtf8)
    {
        int rc = rtAsn1String_CheckSanity(pThis, NULL, NULL, &cch);
        if (RT_FAILURE(rc))
            return rc;
    }

    *pcch = cch;
    return VINF_SUCCESS;
}




RTDECL(int) RTAsn1String_InitEx(PRTASN1STRING pThis, uint32_t uTag, void const *pvValue, size_t cbValue,
                                PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_ZERO(*pThis);
    AssertMsgReturn(uTag < RT_ELEMENTS(g_acbStringTags) && g_acbStringTags[uTag] > 0, ("uTag=%#x\n", uTag),
                    VERR_INVALID_PARAMETER);

    RTAsn1MemInitAllocation(&pThis->Allocation, pAllocator);
    RTAsn1Core_InitEx(&pThis->Asn1Core,
                      uTag,
                      ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                      &g_RTAsn1String_Vtable,
                      RTASN1CORE_F_PRESENT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);

    if (cbValue > 0)
    {
        int rc = RTAsn1ContentDup(&pThis->Asn1Core, pvValue, cbValue, pAllocator);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1String_InitWithValue(PRTASN1STRING pThis, const char *pszUtf8Value, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    Assert(RTStrValidateEncoding(pszUtf8Value));
    return RTAsn1String_InitEx(pThis, ASN1_TAG_UTF8_STRING, pszUtf8Value, strlen(pszUtf8Value), pAllocator);
}


RTDECL(int) RTAsn1String_RecodeAsUtf8(PRTASN1STRING pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    /*
     * Query the UTF-8 string. Do this even if it's already an
     * ASN1_TAG_UTF8_STRING object as it makes sure we've got a valid UTF-8
     * string upon successful return.
     */
    int rc = RTAsn1String_QueryUtf8(pThis, NULL, NULL);
    if (RT_SUCCESS(rc))
    {
        if (RTASN1CORE_GET_TAG(&pThis->Asn1Core) != ASN1_TAG_UTF8_STRING)
        {
            /*
             * Resize the content, copy the UTF-8 bytes in there, and change
             * the tag.
             */
            rc = RTAsn1ContentReallocZ(&pThis->Asn1Core, pThis->cchUtf8, pAllocator);
            if (RT_SUCCESS(rc))
            {
                memcpy((void *)pThis->Asn1Core.uData.pv, pThis->pszUtf8, pThis->cchUtf8);
                rc = RTAsn1Core_ChangeTag(&pThis->Asn1Core, ASN1_TAG_UTF8_STRING);
            }
        }
    }
    return rc;
}



/*
 * ASN.1 STRING - Standard Methods.
 */

RT_DECL_DATA_CONST(RTASN1COREVTABLE const) g_RTAsn1String_Vtable =
{
    "RTAsn1String",
    sizeof(RTASN1STRING),
    UINT8_MAX,
    ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
    0,
    (PFNRTASN1COREVTDTOR)RTAsn1String_Delete,
    NULL,
    (PFNRTASN1COREVTCLONE)RTAsn1String_Clone,
    (PFNRTASN1COREVTCOMPARE)RTAsn1String_Compare,
    (PFNRTASN1COREVTCHECKSANITY)RTAsn1String_CheckSanity,
    NULL,
    NULL
};


RTDECL(int) RTAsn1String_Init(PRTASN1STRING pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    return RTAsn1String_InitEx(pThis, ASN1_TAG_UTF8_STRING, NULL /*pvValue*/, 0 /*cbValue*/, pAllocator);
}


RTDECL(int) RTAsn1String_Clone(PRTASN1STRING pThis, PCRTASN1STRING pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtr(pSrc); AssertPtr(pThis); AssertPtr(pAllocator);
    RT_ZERO(*pThis);
    if (RTAsn1String_IsPresent(pSrc))
    {
        AssertReturn(pSrc->Asn1Core.pOps == &g_RTAsn1String_Vtable, VERR_INTERNAL_ERROR_3);
        int rc = RTAsn1Core_CloneContent(&pThis->Asn1Core, &pSrc->Asn1Core, pAllocator);
        if (RT_SUCCESS(rc))
        {
            /* Don't copy the UTF-8 representation, decode it when queried. */
            RTAsn1MemInitAllocation(&pThis->Allocation, pAllocator);
            return VINF_SUCCESS;
        }
    }
    return VINF_SUCCESS;
}


RTDECL(void) RTAsn1String_Delete(PRTASN1STRING pThis)
{
    if (   pThis
        && RTAsn1String_IsPresent(pThis))
    {
        Assert(pThis->Asn1Core.pOps == &g_RTAsn1String_Vtable);

        if (pThis->Allocation.cbAllocated)
            RTAsn1MemFree(&pThis->Allocation, (char *)pThis->pszUtf8);
        RTAsn1ContentFree(&pThis->Asn1Core);
        RT_ZERO(*pThis);
    }
}


RTDECL(int) RTAsn1String_Enum(PRTASN1STRING pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser)
{
    RT_NOREF_PV(pThis); RT_NOREF_PV(pfnCallback); RT_NOREF_PV(uDepth); RT_NOREF_PV(pvUser);
    Assert(pThis && (!RTAsn1String_IsPresent(pThis) || pThis->Asn1Core.pOps == &g_RTAsn1String_Vtable));

    /* No children to enumerate. */
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1String_Compare(PCRTASN1STRING pLeft, PCRTASN1STRING pRight)
{
    /* Compare tag and binary value. */
    return RTAsn1String_CompareEx(pLeft, pRight, true /*fTypeToo*/);
}


RTDECL(int) RTAsn1String_CheckSanity(PCRTASN1STRING pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    RT_NOREF_PV(fFlags);
    if (RT_UNLIKELY(!RTAsn1String_IsPresent(pThis)))
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Missing (STRING).", pszErrorTag);
    return rtAsn1String_CheckSanity(pThis, pErrInfo, pszErrorTag, NULL /*pcchUtf8*/);
}


/*
 * Generate code for the tag specific methods.
 * Note! This is very similar to what we're doing in asn1-ut-time.cpp.
 */
#define RTASN1STRING_IMPL(a_uTag, a_szTag, a_Api) \
    \
    RTDECL(int) RT_CONCAT(a_Api,_Init)(PRTASN1STRING pThis, PCRTASN1ALLOCATORVTABLE pAllocator) \
    { \
        return RTAsn1String_InitEx(pThis, a_uTag, NULL /*pvValue*/, 0 /*cbValue*/, pAllocator); \
    } \
    \
    RTDECL(int) RT_CONCAT(a_Api,_Clone)(PRTASN1STRING pThis, PCRTASN1STRING pSrc, PCRTASN1ALLOCATORVTABLE pAllocator) \
    { \
        AssertReturn(RTASN1CORE_GET_TAG(&pSrc->Asn1Core) == a_uTag || !RTAsn1String_IsPresent(pSrc), \
                     VERR_ASN1_STRING_TAG_MISMATCH); \
        return RTAsn1String_Clone(pThis, pSrc, pAllocator); \
    } \
    \
    RTDECL(void) RT_CONCAT(a_Api,_Delete)(PRTASN1STRING pThis) \
    { \
        Assert(   !pThis \
               || !RTAsn1String_IsPresent(pThis) \
               || (   pThis->Asn1Core.pOps == &g_RTAsn1String_Vtable \
                   && RTASN1CORE_GET_TAG(&pThis->Asn1Core) == a_uTag) ); \
        RTAsn1String_Delete(pThis); \
    } \
    \
    RTDECL(int) RT_CONCAT(a_Api,_Enum)(PRTASN1STRING pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser) \
    { \
        RT_NOREF_PV(pThis); RT_NOREF_PV(pfnCallback); RT_NOREF_PV(uDepth); RT_NOREF_PV(pvUser); \
        Assert(   pThis \
               && (   !RTAsn1String_IsPresent(pThis) \
                   || (   pThis->Asn1Core.pOps == &g_RTAsn1String_Vtable \
                       && RTASN1CORE_GET_TAG(&pThis->Asn1Core) == a_uTag) ) ); \
        /* No children to enumerate. */ \
        return VINF_SUCCESS; \
    } \
    \
    RTDECL(int) RT_CONCAT(a_Api,_Compare)(PCRTASN1STRING pLeft, PCRTASN1STRING pRight) \
    { \
        int iDiff = RTAsn1String_CompareEx(pLeft, pRight, true /*fTypeToo*/); \
        if (!iDiff && RTASN1CORE_GET_TAG(&pLeft->Asn1Core) != a_uTag && RTAsn1String_IsPresent(pLeft)) \
            iDiff = RTASN1CORE_GET_TAG(&pLeft->Asn1Core) < a_uTag ? -1 : 1; \
        return iDiff; \
    } \
    \
    RTDECL(int) RT_CONCAT(a_Api,_CheckSanity)(PCRTASN1STRING pThis, uint32_t fFlags, \
                                              PRTERRINFO pErrInfo, const char *pszErrorTag) \
    { \
        if (RTASN1CORE_GET_TAG(&pThis->Asn1Core) != a_uTag && RTAsn1String_IsPresent(pThis)) \
            return RTErrInfoSetF(pErrInfo, VERR_ASN1_STRING_TAG_MISMATCH, "%s: uTag=%#x, expected %#x (%s)", \
                                 pszErrorTag, RTASN1CORE_GET_TAG(&pThis->Asn1Core), a_uTag, a_szTag); \
        return RTAsn1String_CheckSanity(pThis, fFlags, pErrInfo, pszErrorTag); \
    }

#include "asn1-ut-string-template2.h"


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-string-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-core.h>
#include <iprt/asn1-generator-init.h>
#include <iprt/asn1-generator-sanity.h>

