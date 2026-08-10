/* $Id: textcache.cpp 114912 2026-08-10 12:03:20Z vitali.pelenjow@oracle.com $ */
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

#include "utils.h"
#include "shadowbuffer.h"
#include "textcache.h"

/*
 * VRDP TEXT cache.
 *
 * The client's glyph cache consists of 10 caches, which are called 'fonts' in RDP:
 * font handle:     0     1     2     3     4     5     6     7     8     9
 * glyph size:    0x4   0x4   0x8   0x8  0x10  0x20  0x40  0x80 0x100 0x800
 * glyphs:       0xfe  0xfe  0xfe  0xfe  0xfe  0xfe  0xfe  0xfe  0xfe  0x40
 *
 * Glyph size is the size of the 1 BPP glyph bitmap bytes rounded up to 32 bit dword:
 *   glyph size = (((w + 7) / 8) * h + 3) & ~3
 *
 * The text cache is on the OUTPUT thread.
 *
 * There is the server glyphs cache that stores glyph information
 * and is responsible for the glyph(font, index) assignments.
 *
 * Each client object has a map which glyphs were
 * cached by this particular client.
 *
 */

/* The server uses following font handles:
 * server font index:        0     1     2     3     4     5     6
 * RDP font handle:          0     2     4     5     6     7     8
 * glyph size (max):       0x4   0x8  0x10  0x20  0x40  0x80 0x100
 * glyphs:                0xfe  0xfe  0xfe  0xfe  0xfe  0xfe  0xfe
 */

typedef struct _TCGLYPHFONT
{
    /* Backlink to the cache. */
    PTEXTCACHE ptc;

    /* The index on the font in the server array. */
    int index;
    /* The RDP font handle for the font. */
    int iRDPFontHandle;

    /* The unique server font generation number, it is increased
     * when the glyphs assigment is changed.
     */
    uint32_t u32Uniq;

    unsigned cGlyphsMax;
    unsigned cGlyphsCached;
    TCCACHEDGLYPH aGlyphs[VRDP_TC_NUM_GLYPHS];

} TCGLYPHFONT;

typedef struct _TCGLYPHCACHE
{
    TCGLYPHFONT fonts[VRDP_TC_NUM_FONTS];

    /* Preallocated memory for glyph bitmaps. */
    uint8_t au8GlyphBitmap0  [VRDP_TC_NUM_GLYPHS] [0x4];
    uint8_t au8GlyphBitmap2  [VRDP_TC_NUM_GLYPHS] [0x8];
    uint8_t au8GlyphBitmap4  [VRDP_TC_NUM_GLYPHS] [0x10];
    uint8_t au8GlyphBitmap5  [VRDP_TC_NUM_GLYPHS] [0x20];
    uint8_t au8GlyphBitmap6  [VRDP_TC_NUM_GLYPHS] [0x40];
    uint8_t au8GlyphBitmap7  [VRDP_TC_NUM_GLYPHS] [0x80];
    uint8_t au8GlyphBitmap8  [VRDP_TC_NUM_GLYPHS] [0x100];

} TCGLYPHCACHE;

/*
 * The text cache.
 */
typedef struct _TEXTCACHE
{
    /* Information about cached glyphs. */
    TCGLYPHCACHE glyphs;
} TEXTCACHE;

/* Return pointer to a preallocated memory block that is big enough to store the given font glyph. */
static uint8_t *tcGlyphBitmap (const TCGLYPHFONT *pFont, int iGlyph)
{
    Assert (iGlyph < VRDP_TC_NUM_GLYPHS);

    switch (pFont->iRDPFontHandle)
    {
        case 0: return &pFont->ptc->glyphs.au8GlyphBitmap0 [iGlyph][0];
        case 2: return &pFont->ptc->glyphs.au8GlyphBitmap2 [iGlyph][0];
        case 4: return &pFont->ptc->glyphs.au8GlyphBitmap4 [iGlyph][0];
        case 5: return &pFont->ptc->glyphs.au8GlyphBitmap5 [iGlyph][0];
        case 6: return &pFont->ptc->glyphs.au8GlyphBitmap6 [iGlyph][0];
        case 7: return &pFont->ptc->glyphs.au8GlyphBitmap7 [iGlyph][0];
        case 8: return &pFont->ptc->glyphs.au8GlyphBitmap8 [iGlyph][0];
    }

    AssertFailed ();
    return NULL;
}

/* Return maximum size of a bitmap for this font. */
static uint32_t tcGlyphBitmapMaxSize (const TCGLYPHFONT *pFont)
{
    switch (pFont->iRDPFontHandle)
    {
        case 0: return sizeof (pFont->ptc->glyphs.au8GlyphBitmap0 [0]);
        case 2: return sizeof (pFont->ptc->glyphs.au8GlyphBitmap2 [0]);
        case 4: return sizeof (pFont->ptc->glyphs.au8GlyphBitmap4 [0]);
        case 5: return sizeof (pFont->ptc->glyphs.au8GlyphBitmap5 [0]);
        case 6: return sizeof (pFont->ptc->glyphs.au8GlyphBitmap6 [0]);
        case 7: return sizeof (pFont->ptc->glyphs.au8GlyphBitmap7 [0]);
        case 8: return sizeof (pFont->ptc->glyphs.au8GlyphBitmap8 [0]);
    }

    AssertFailed ();
    return 0;
}

/* Convert the server font array index to the RDP font handle. */
static int tcRDPHandleFromServerIndex (int index)
{
    switch (index)
    {
        case 0: return 0;
        case 1: return 2;
        case 2: return 4;
        case 3: return 5;
        case 4: return 6;
        case 5: return 7;
        case 6: return 8;
        default: break;
    }

    AssertFailed (); /* Should not happen, if the guest additions are OK. */
    return -1;
}

/* Convert the RDP font handle to the server font array index. */
static int tcServerIndexFromRDPHandle (int handle)
{
    switch (handle)
    {
        case 0: return 0;
        case 2: return 1;
        case 4: return 2;
        case 5: return 3;
        case 6: return 4;
        case 7: return 5;
        case 8: return 6;
        default: break;
    }

    AssertFailed (); /* Should not happen, if the guest additions are OK. */
    return -1;
}


static int tcSelectRDPHandle (PTEXTCACHE ptc, const VRDEORDERTEXT *pOrder)
{
    NOREF (ptc);

    /* Choose the RDP font handle using the maximum glyph size of the font.
     * This unefficiently uses lower cache indexes, but good enough for the first version.
     *
     * Fonts 0, 1 and 2, 3 are selected as 0 and 2 respectively for simplicity.
     * Font handle 1 is not used because such small glyphs are rare.
     *
     * @todo Actually it would be good to have 2 and 3 working together.
     *       8x8 is rather common size for a glyph. But this makes
     *       the code much more complex and will require to choose
     *       the handle using actual glyph sizes rather than
     *       the maximum glyph size of a font.
     */

    uint16_t u16MaxGlyph = pOrder->u16MaxGlyph;

    if (u16MaxGlyph <= 0x04)  return 0;
    if (u16MaxGlyph <= 0x08)  return 2;
    if (u16MaxGlyph <= 0x10)  return 4;
    if (u16MaxGlyph <= 0x20)  return 5;
    if (u16MaxGlyph <= 0x40)  return 6;
    if (u16MaxGlyph <= 0x80)  return 7;
    if (u16MaxGlyph <= 0x100) return 8;

    AssertFailed (); /* Should not really happen if the guest additions are OK. */
    return -1;
}

static void tcClearFontCache (TCGLYPHFONT *pFont)
{
    if (++pFont->u32Uniq == 0)
    {
        pFont->u32Uniq = 1;
    }

    pFont->cGlyphsCached = 0;
}

static TCCACHEDGLYPH *tcCacheGlyph (TCGLYPHFONT *pFont, const VRDEORDERGLYPH *pGlyph, uint32_t cbGlyphBitmap)
{
    if (pFont->cGlyphsCached >= pFont->cGlyphsMax)
    {
        /* No place for the new glyph. */
        return NULL;
    }

    if (cbGlyphBitmap > tcGlyphBitmapMaxSize (pFont))
    {
        return NULL;
    }

    /* Allocate place for the new glyph, remember its index. */
    int iGlyph = pFont->cGlyphsCached++;

    /* Convert the VRDEORDERGLYPH to the cache glyph format. */
    TCCACHEDGLYPH *pCachedGlyph = &pFont->aGlyphs[iGlyph];

    pCachedGlyph->u8Index = (uint8_t)iGlyph;

    pCachedGlyph->u64Handle = pGlyph->u64Handle;

    pCachedGlyph->w = pGlyph->w;
    pCachedGlyph->h = pGlyph->h;

    pCachedGlyph->xOrigin = pGlyph->xOrigin;
    pCachedGlyph->yOrigin = pGlyph->yOrigin;

    pCachedGlyph->pu8Bitmap = tcGlyphBitmap (pFont, iGlyph);

    memcpy (pCachedGlyph->pu8Bitmap, pGlyph->au8Bitmap, cbGlyphBitmap);

    return pCachedGlyph;
}


static TCCACHEDGLYPH *tcFindCachedGlyph (TCGLYPHFONT *pFont, const VRDEORDERGLYPH *pGlyph)
{
    TCCACHEDGLYPH *pCachedGlyph = &pFont->aGlyphs[0];

    unsigned i;

    for (i = 0; i < pFont->cGlyphsCached; i++, pCachedGlyph++)
    {
        if (pCachedGlyph->u64Handle == pGlyph->u64Handle)
        {
            return pCachedGlyph;
        }
    }

    return NULL;
}


static int tcTryCacheGlyphs (const VRDEORDERTEXT *pOrder, uint32_t cbOrder, TCGLYPHFONT *pFont, TCFONTTEXT2 *pFontText2)
{
    int rc = VINF_SUCCESS;

    /* Scan the string and check glyphs. */
    const VRDEORDERGLYPH *pGlyph = (VRDEORDERGLYPH *)(&pOrder[1]); /* Glyphs follow the order structure. */
    uint32_t cbLeft = cbOrder;

    unsigned i;
    for (i = 0; i < pOrder->u8Glyphs; i++)
    {
        if (cbLeft < RT_UOFFSETOF(VRDEORDERGLYPH, au8Bitmap))
        {
            rc = VERR_INVALID_PARAMETER;
            break;
        }

        if (pGlyph->o32NextGlyph > cbLeft)
        {
            rc = VERR_INVALID_PARAMETER;
            break;
        }

        uint32_t cbGlyphBitmap = (pGlyph->w + 7) / 8; /* Line size in bytes. */
        cbGlyphBitmap *= pGlyph->h;                   /* Size of bitmap. */
        cbGlyphBitmap = (cbGlyphBitmap + 3) & ~3;     /* 32 bit DWORD align. */

        if (cbGlyphBitmap > cbLeft - RT_UOFFSETOF(VRDEORDERGLYPH, au8Bitmap))
        {
            rc = VERR_INVALID_PARAMETER;
            break;
        }

        if (cbGlyphBitmap > pOrder->u16MaxGlyph)
        {
            rc = VERR_INVALID_PARAMETER;
            break;
        }

        /* Find the glyph in the cache. */
        TCCACHEDGLYPH *pCachedGlyph = tcFindCachedGlyph (pFont, pGlyph);

        if (!pCachedGlyph)
        {
            pCachedGlyph = tcCacheGlyph (pFont, pGlyph, cbGlyphBitmap);
        }

        if (!pCachedGlyph)
        {
            /* Could not cache the glyph. The reason is that the cache is full (or whatever). */
            rc = VERR_NOT_SUPPORTED;
            break;
        }

        /* The glyph is in the cache. Update the pFontText2. */
        pFontText2->aGlyphs[i].pCachedGlyph = pCachedGlyph;
        if (pOrder->u8Flags & VRDP_TEXT2_CHAR_INC_EQUAL_BM_BASE)
        {
            /* Monospaced font. */
            pFontText2->aGlyphs[i].x = 0;
            pFontText2->aGlyphs[i].y = 0;
        }
        else
        {
            /* Not monospaced font. */
            pFontText2->aGlyphs[i].x = pGlyph->x;
            pFontText2->aGlyphs[i].y = pGlyph->y;
        }
        pFontText2->cGlyphs++;

        pGlyph = (const VRDEORDERGLYPH *)((uint8_t *)pGlyph + pGlyph->o32NextGlyph);
        cbLeft -= pGlyph->o32NextGlyph;
    }

    return rc;
}

static int tcSetupFontText2 (TCFONTTEXT2 *pFontText2, TCGLYPHFONT *pFont, const VRDEORDERTEXT *pOrder)
{
    pFontText2->ptc         = pFont->ptc;

    pFontText2->u32Uniq     = pFont->u32Uniq;
    pFontText2->index       = pFont->index;

    pFontText2->u8RDPFontHandle = (uint8_t)pFont->iRDPFontHandle;

    pFontText2->u8Flags     = pOrder->u8Flags;
    pFontText2->u8CharInc   = pOrder->u8CharInc;

    pFontText2->rgbFG       = pOrder->u32FgRGB;
    pFontText2->rgbBG       = pOrder->u32BgRGB;

    pFontText2->bkground.left   = pOrder->xBkGround;
    pFontText2->bkground.top    = pOrder->yBkGround;
    pFontText2->bkground.right  = pOrder->xBkGround + pOrder->wBkGround;
    pFontText2->bkground.bottom = pOrder->yBkGround + pOrder->hBkGround;

    pFontText2->opaque.left   = pOrder->xOpaque;
    pFontText2->opaque.top    = pOrder->yOpaque;
    pFontText2->opaque.right  = pOrder->xOpaque + pOrder->wOpaque;
    pFontText2->opaque.bottom = pOrder->yOpaque + pOrder->hOpaque;

    /* Get the position of the first glyph. */
    VRDEORDERGLYPH *pGlyph = (VRDEORDERGLYPH *)(&pOrder[1]); /* Glyphs follow the order structure. */

    /* This seems to be correct. But have to verify different fonts and text directions. */
    pFontText2->origin.x    = pGlyph->x;
    pFontText2->origin.y    = pGlyph->y;

    return 0;
}


bool TCCacheGlyphs (PTEXTCACHE ptc, const VRDEORDERTEXT *pOrder, uint32_t cbOrder, TCFONTTEXT2 **ppFontText2)
{
    /* Check which glyphs are already cached. The original order is copied
     * to the TCFONTTEXT2 structure and the status of each glyph is determined.
     * TCFONTTEXT2 contains an array of glyphs.
     *
     * The generation has 2 passes. First, try to use already caches glyphs, if a new
     * glyph has to be cached and there is no place for it in the cache, then entire
     * cache is cleared and the second caching pass is performed.
     *
     * String longer than 3 chars are considered as fragments and
     * are put to the fragment cache. The text2 order then adds 0xff ID CB
     * to the string: ID - the cache index. CB is the string length.
     */

    if (cbOrder < sizeof(VRDEORDERTEXT))
    {
        return false;
    }

    int iRDPFontHandle = tcSelectRDPHandle (ptc, pOrder);

    if (iRDPFontHandle == -1)
    {
        return false;
    }

    TCFONTTEXT2 *pFontText2 = (TCFONTTEXT2 *)VRDPMemAllocZ (sizeof (TCFONTTEXT2));

    if (!pFontText2)
    {
        return false;
    }

    TCGLYPHFONT *pFont = &ptc->glyphs.fonts[ tcServerIndexFromRDPHandle (iRDPFontHandle) ];

    int rc = tcTryCacheGlyphs (pOrder, cbOrder, pFont, pFontText2);

    if (RT_FAILURE(rc))
    {
        tcClearFontCache (pFont);

        memset (pFontText2, 0, sizeof (TCFONTTEXT2));

        rc = tcTryCacheGlyphs (pOrder, cbOrder, pFont, pFontText2);
    }

    if (RT_SUCCESS (rc))
    {
        tcSetupFontText2 (pFontText2, pFont, pOrder);

        *ppFontText2 = pFontText2;

        return true;
    }

    TCFreeFontText2 (pFontText2);

    return false;
}

void TCFreeFontText2 (TCFONTTEXT2 *pFontText2)
{
    VRDPMemFree (pFontText2);
}

/*
 * Allocate and initialize a new cache.
 */
TEXTCACHE *TCCreate (void)
{
    TEXTCACHE *ptc = (TEXTCACHE *)VRDPMemAllocZ (sizeof (TEXTCACHE));

    if (ptc)
    {
        int i;
        for (i = 0; i < VRDP_TC_NUM_FONTS; i++)
        {
            TCGLYPHFONT *pFont = &ptc->glyphs.fonts[i];

            pFont->ptc            = ptc;
            pFont->index          = i;
            pFont->iRDPFontHandle = tcRDPHandleFromServerIndex (i);
            pFont->u32Uniq        = 1;
            pFont->cGlyphsMax     = VRDP_TC_NUM_GLYPHS;
            pFont->cGlyphsCached  = 0;

            /* Verify the mapping between RDP handles and server indexes. */
            Assert (tcServerIndexFromRDPHandle (ptc->glyphs.fonts[i].iRDPFontHandle) == i);
        }
    }

    return ptc;
}

/*
 * Deallocate cache.
 */
void TCDelete (TEXTCACHE *ptc)
{
    if (ptc)
    {
        VRDPMemFree (ptc);
    }
}
