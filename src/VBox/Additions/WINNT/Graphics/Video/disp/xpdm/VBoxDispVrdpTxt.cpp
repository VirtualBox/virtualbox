/* $Id: VBoxDispVrdpTxt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#include "VBoxDisp.h"
#include <VBox/RemoteDesktop/VRDEOrders.h>
#include <iprt/crc.h>

/*
 * The client's glyph cache theoretically consists of 10 caches:
 * cache index:         0     1     2     3     4     5     6     7     8     9
 * glyph size (max):  0x4   0x4   0x8   0x8  0x10  0x20  0x40  0x80 0x100 0x800
 * glyphs:           0xfe  0xfe  0xfe  0xfe  0xfe  0xfe  0xfe  0xfe  0xfe  0x40
 *
 * Glyph size is the size of the 1 BPP glyph bitmap bytes rounded up to 32 bit dword:
 *   glyph size = (((w + 7) / 8) * h + 3) & ~3
 *
 * Following simplifications are used:
 *   * Cache index 9 is not used, such huge glyphs (~40x40 pixel) are unlikely,
 *     (especially for raster fonts) so without it all caches contain up to 0xfe
 *     characters.
 *   * Maximum string length is 0xfe, so a string can always
 *     be placed in the cache, even if the string consists of
 *     all different characters.
 *
 * The driver always sends glyphs to the host.
 * The host maintains the glyph cache. Performance issues:
 * - increased the CPU load to copy glyph info.
 * + eliminates the driver side of the cache.
 * + lets the host to optimize memory usage.
 *
 * Therefore, on a textout the driver must send to the host
 * The string attributes:
 *     - number of glyphs;
 *     - flags: HORIZONTAL, VERTICAL, CHAR_INC_EQUAL_BM_BASE, ... (1);
 *     - glyph increment for monospaced font (== 0 for not monospaced font) or a flag fMonospaced;
 *     - the bounding box of the string background (prclOpaque or the pstro->rclBkGround);
 *     - the foreground and background colors;
 *     - the mix (two ROP2);
 *     - ... (1).
 * The glyph information for each glyph in the string:
 *     - unique glyph handle 64 bit for use of crc64;
 *     - the glyph bitmap coordinates on the screen;
 *     - width, height of the glyph;
 *     - the glyph origin in the bitmap (2);
 *     - the 1BPP glyph bitmap;
 *     - whether it is a 'space' character (3);
 *     - ... (1).
 *
 * Remarks:
 *   (1) to be defined;
 *   (2) might be not necessary;
 *   (3) it seems to be not necessary to know the codepoint value,
 *       strings are considered to be a set of bitmaps from
 *       the cache space. But reporting that the glyph actually
 *       represents a 'space' character might allow some optimizations.
 *
 * The VRDEORDERTEXT consists of the string info and glyph infos.
 *
 */

static BOOL vrdpReportGlyph(GLYPHPOS *pGlyphPos, uint8_t **ppu8Ptr, uint8_t *pu8End)
{
    uint32_t cbOrder;
    uint32_t cbBitmap;

    VRDEORDERGLYPH *pOrder = (VRDEORDERGLYPH *)*ppu8Ptr;

    GLYPHBITS *pgb = pGlyphPos->pgdf->pgb;

    /* BYTE-aligned 1BPP bitmap of the glyph. The array includes padding at the end to DWORD-align. */
    cbBitmap = (pgb->sizlBitmap.cx + 7) / 8; /* Line size in bytes. */
    cbBitmap *= pgb->sizlBitmap.cy;          /* Size of bitmap. */
    cbBitmap = (cbBitmap + 3) & ~3;          /* DWORD align. */

    cbOrder = (uint32_t)((uint8_t *)&pOrder->au8Bitmap - (uint8_t *)pOrder);

    cbOrder += cbBitmap;

    if (*ppu8Ptr + cbOrder > pu8End)
    {
        return FALSE;
    }

    pOrder->o32NextGlyph = cbOrder;

    pOrder->u64Handle = RTCrc64Start();
    pOrder->u64Handle = RTCrc64Process(pOrder->u64Handle, pgb->aj, cbBitmap);
    pOrder->u64Handle = RTCrc64Process(pOrder->u64Handle, &pgb->ptlOrigin, sizeof (pgb->ptlOrigin));
    pOrder->u64Handle = RTCrc64Finish(pOrder->u64Handle);

    pOrder->x = (int16_t)pGlyphPos->ptl.x;
    pOrder->y = (int16_t)pGlyphPos->ptl.y;

    pOrder->w = (uint16_t)pgb->sizlBitmap.cx;
    pOrder->h = (uint16_t)pgb->sizlBitmap.cy;

    pOrder->xOrigin = (int16_t)pgb->ptlOrigin.x;
    pOrder->yOrigin = (int16_t)pgb->ptlOrigin.y;

    /* 1BPP bitmap. Rows are byte aligned. Size is (((w + 7)/8) * h + 3) & ~3. */
    memcpy (pOrder->au8Bitmap, pgb->aj, cbBitmap);

    *ppu8Ptr += cbOrder;

    return TRUE;
}

static uint32_t vrdpSizeofTextOrder(ULONG cGlyphs, ULONG cbMaxGlyph)
{
    uint32_t cb = sizeof (VRDEORDERTEXT);

    cb += cGlyphs * (sizeof (VRDEORDERGLYPH) + cbMaxGlyph);

    return cb;
}

BOOL vrdpReportText(PVBOXDISPDEV pDev, VRDPCLIPRECTS *pClipRects, STROBJ *pstro, FONTOBJ *pfo,
                    RECTL *prclOpaque, ULONG ulForeRGB, ULONG ulBackRGB)
{
    FONTINFO fi;
    uint32_t cbOrderMax;
    VRDEORDERTEXT *pOrder;
    BOOL fResult;
    uint8_t *pu8GlyphPtr;
    uint8_t *pu8GlyphEnd;

    LOGF(("pDev %p, pClipRects %p, pstro %p, pfo %p, prclOpaque %p, ulForeRGB %x, ulBackRGB %x",
          pDev, pClipRects, pstro, pfo, prclOpaque, ulForeRGB, ulBackRGB));

    if (pstro->ulCharInc > 0xFF)
    {
        return FALSE;
    }

    /* The driver can get vertical strings with both SO_HORIZONTAL and SO_VERTICAL bits equal to zero. */
    if (   (pstro->flAccel & SO_HORIZONTAL) == 0
        || (pstro->flAccel & SO_REVERSED) != 0)
    {
        /* Do not support (yet) the vertical and right to left strings.
         * @todo implement and test.
         */
        return FALSE;
    }

    memset (&fi, 0, sizeof (fi));

    FONTOBJ_vGetInfo (pfo, sizeof (fi), &fi);

    if (   fi.cjMaxGlyph1 == 0
        || fi.cjMaxGlyph1 > VRDP_TEXT_MAX_GLYPH_SIZE)
    {
        /* No 1BPP bitmaps or the bitmap is larger than the cache supports. */
        LOG(("fi.cjMaxGlyph1 = %x. Return FALSE", fi.cjMaxGlyph1));
        return FALSE;
    }

    cbOrderMax = vrdpSizeofTextOrder(pstro->cGlyphs, fi.cjMaxGlyph1);

    LOG(("pstro->cGlyphs = %d, fi.cjMaxGlyph1 = 0x%x, cbOrderMax = 0x%x.", pstro->cGlyphs, fi.cjMaxGlyph1, cbOrderMax));

    pOrder = (VRDEORDERTEXT *)EngAllocMem(0, cbOrderMax, MEM_ALLOC_TAG);

    if (!pOrder)
    {
        LOG(("pOrder = %x. Return FALSE", pOrder));
        return FALSE;
    }

    pu8GlyphPtr = (uint8_t *)&pOrder[1]; /* Follows the order header. */
    pu8GlyphEnd = (uint8_t *)pOrder + cbOrderMax;

    pOrder->xBkGround = (int16_t)pstro->rclBkGround.left;
    pOrder->yBkGround = (int16_t)pstro->rclBkGround.top;
    pOrder->wBkGround = (uint16_t)(pstro->rclBkGround.right - pstro->rclBkGround.left);
    pOrder->hBkGround = (uint16_t)(pstro->rclBkGround.bottom - pstro->rclBkGround.top);

    if (prclOpaque)
    {
        pOrder->xOpaque = (int16_t)prclOpaque->left;
        pOrder->yOpaque = (int16_t)prclOpaque->top;
        pOrder->wOpaque = (uint16_t)(prclOpaque->right - prclOpaque->left);
        pOrder->hOpaque = (uint16_t)(prclOpaque->bottom - prclOpaque->top);
    }
    else
    {
        pOrder->xOpaque = 0;
        pOrder->yOpaque = 0;
        pOrder->wOpaque = 0;
        pOrder->hOpaque = 0;
    }

    pOrder->u16MaxGlyph = (uint16_t)fi.cjMaxGlyph1;

    pOrder->u8Glyphs = (uint8_t)pstro->cGlyphs;

    pOrder->u8Flags = (uint8_t)pstro->flAccel;

    pOrder->u8CharInc = (uint8_t)pstro->ulCharInc;

    pOrder->u32FgRGB = ulForeRGB;
    pOrder->u32BgRGB = ulBackRGB;

    LOG(("pstro->pgp %p.", pstro->pgp));

    /* Enumerate glyphs. */
    STROBJ_vEnumStart(pstro);

    fResult = TRUE;

    for (;;)
    {
        ULONG i;
        ULONG cGlyphs = 0;
        GLYPHPOS *pGlyphPos = NULL;

        BOOL fMore = STROBJ_bEnum (pstro, &cGlyphs, &pGlyphPos);

        LOG(("cGlyphs %d.", cGlyphs));

        for (i = 0; i < cGlyphs; i++)
        {
            fResult = vrdpReportGlyph(&pGlyphPos[i], &pu8GlyphPtr, pu8GlyphEnd);

            if (!fResult)
            {
                break;
            }
        }

        if (!fMore || !fResult)
        {
            break;
        }
    }

    LOG(("fResult %d", fResult));

    if (fResult)
    {
        pOrder->cbOrder = (uint32_t)(pu8GlyphPtr - (uint8_t *)pOrder);

        vrdpReportOrderGeneric(pDev, pClipRects, pOrder, pOrder->cbOrder, VRDE_ORDER_TEXT);
    }

    EngFreeMem(pOrder);

    return fResult;
}
