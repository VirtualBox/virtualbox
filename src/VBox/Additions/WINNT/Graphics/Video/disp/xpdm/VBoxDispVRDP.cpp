/* $Id: VBoxDispVRDP.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#define VRDP_MAKE_OP(__c) (__c)

/* vrdpGetIntersectingClipRects result */
#define VRDP_CLIP_OK              0
#define VRDP_CLIP_NO_INTERSECTION 1
#define VRDP_CLIP_TOO_MANY_RECTS  2

typedef struct _VRDPBRUSH
{
   BOOL fPattern;

   union {
       struct {
           uint32_t rgbFG;
           uint32_t rgbBG;
           uint8_t au8Pattern[8];
       } pat;

       struct {
           uint16_t w;
           uint16_t h;
           uint32_t au32Bits[1];
           /* Here bits continue. */
       } bitmap;
   } u;
} VRDPBRUSH;

#if 1
#define dumpPCO(a, b) do {} while (0)
#else
static void dumpPCO(RECTL *prclTrg, CLIPOBJ *pco)
{
    LOG(("pco = %p Trg = %d-%d %d-%d", pco, prclTrg->left, prclTrg->right, prclTrg->top, prclTrg->bottom));

    if (pco)
    {
        BOOL bMore;
        CLIPRECTS cr;
        RECTL* prclClip;
        int cRects = 0;

        LOG(("pco = %d %d-%d %d-%d dc %d fc %d mode %d opt %d",
                 pco->iUniq,
                 pco->rclBounds.left, pco->rclBounds.right, pco->rclBounds.top, pco->rclBounds.bottom,
                 pco->iDComplexity, pco->iFComplexity, pco->iMode, pco->fjOptions));

        CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_ANY, 0);

        do
        {
            bMore = CLIPOBJ_bEnum(pco, sizeof(cr), (ULONG*)&cr);

            for (prclClip = &cr.arcl[0]; cr.c != 0; cr.c--, prclClip++)
            {
                LOG(("      %d-%d %d-%d", prclClip->left, prclClip->right, prclClip->top, prclClip->bottom));
                cRects++;
            }
        } while (bMore);

        LOG(("Total %d rects", cRects));
    }
}
#endif

static uint32_t vrdpColor2RGB (SURFOBJ *pso, uint32_t color)
{
    uint32_t rgb = 0;

    switch (pso->iBitmapFormat)
    {
        case BMF_16BPP:
        {
            uint8_t *d = (uint8_t *)&rgb;

            *d++ = (BYTE)( color        << 3); /* B */
            *d++ = (BYTE)((color >> 5)  << 2); /* G */
            *d++ = (BYTE)((color >> 11) << 3); /* R */
        } break;
        case BMF_24BPP:
        case BMF_32BPP:
        {
            rgb = color & 0xFFFFFF;
        } break;
        default:
            WARN(("Unsupported bitmap format %d", pso->iBitmapFormat));
    }

    return rgb;
}

static void vrdpPointFX2Point(const POINTFIX *pptfx, VRDEORDERPOINT *ppt)
{
    ppt->x = (int16_t)FXTOLROUND(pptfx->x);
    ppt->y = (int16_t)FXTOLROUND(pptfx->y);
}

static void vrdpPolyPointsAdd(VRDEORDERPOLYPOINTS *pPoints, const VRDEORDERPOINT *ppt)
{
    Assert(pPoints->c < RT_ELEMENTS(pPoints->a));

    pPoints->a[pPoints->c] = *ppt;

    pPoints->c++;
}

static void vrdpExtendOrderBounds(VRDEORDERBOUNDS *pBounds, const VRDEORDERPOINT *ppt)
{
    /* Bounds have inclusive pt1 and exclusive pt2. */

    if (pBounds->pt1.x > ppt->x)        /* Left. */
    {
        pBounds->pt1.x = ppt->x;
    }
    if (pBounds->pt1.y > ppt->y)        /* Top. */
    {
        pBounds->pt1.y = ppt->y;
    }
    if (pBounds->pt2.x <= ppt->x)       /* Right. */
    {
        pBounds->pt2.x = ppt->x + 1;
    }
    if (pBounds->pt2.y <= ppt->y)       /* Bottom. */
    {
        pBounds->pt2.y = ppt->y + 1;
    }
}

static void vrdpOrderRect(RECTL *prcl)
{
    int tmp;

    if (prcl->left > prcl->right)
    {
        WARN(("Inverse X coordinates"));

        tmp = prcl->left;
        prcl->left = prcl->right;
        prcl->right = tmp;
    }

    if (prcl->top > prcl->bottom)
    {
        WARN(("Inverse Y coordinates"));

        tmp = prcl->top;
        prcl->top = prcl->bottom;
        prcl->bottom = tmp;
    }
}

static BOOL vrdpIsRectEmpty (const RECTL *prcl)
{
    return (prcl->left == prcl->right) || (prcl->top == prcl->bottom);
}

static void vrdpIntersectRects(RECTL *prectResult, const RECTL *prect1, const RECTL *prect2)
{
    /* Calculations are easier with left, right, top, bottom. */
    int xLeft1   = prect1->left;
    int xRight1  = prect1->right;

    int xLeft2   = prect2->left;
    int xRight2  = prect2->right;

    int yTop1    = prect1->top;
    int yBottom1 = prect1->bottom;

    int yTop2    = prect2->top;
    int yBottom2 = prect2->bottom;

    int xLeftResult = max (xLeft1, xLeft2);
    int xRightResult = min (xRight1, xRight2);

    /* Initialize result to empty record. */
    memset (prectResult, 0, sizeof (RECTL));

    if (xLeftResult < xRightResult)
    {
        /* There is intersection by X. */

        int yTopResult = max (yTop1, yTop2);
        int yBottomResult = min (yBottom1, yBottom2);

        if (yTopResult < yBottomResult)
        {
            /* There is intersection by Y. */

            prectResult->left   = xLeftResult;
            prectResult->top    = yTopResult;
            prectResult->right  = xRightResult;
            prectResult->bottom = yBottomResult;
        }
    }

    return;
}

void vrdpAdjustRect(SURFOBJ *pso, RECTL *prcl)
{
    int x;
    int y;
    int w;
    int h;

    LOGF(("%d-%d %d-%d on %dx%d\n", prcl->left, prcl->right, prcl->top, prcl->bottom, pso->sizlBitmap.cx, pso->sizlBitmap.cy));

    if (prcl->left <= prcl->right)
    {
        x = prcl->left;
        w = prcl->right - prcl->left;
    }
    else
    {
        WARN(("Inverse X coordinates"));
        x = prcl->right;
        w = prcl->left - prcl->right;
    }

    if (prcl->top <= prcl->bottom)
    {
        y = prcl->top;
        h = prcl->bottom - prcl->top;
    }
    else
    {
        WARN(("Inverse Y coordinates"));
        y = prcl->bottom;
        h = prcl->top - prcl->bottom;
    }

    Assert(w >= 0 && h >= 0);

    /* Correct negative x and y coordinates. */
    if (x < 0)
    {
        x += w; /* Compute xRight which is also the new width. */

        w = (x < 0)? 0: x;

        x = 0;
    }

    if (y < 0)
    {
        y += h; /* Compute xBottom, which is also the new height. */

        h = (y < 0)? 0: y;

        y = 0;
    }

    /* Also check if coords are greater than the display resolution. */
    if (x + w > pso->sizlBitmap.cx)
    {
        w = pso->sizlBitmap.cx > x? pso->sizlBitmap.cx - x: 0;
    }

    if (y + h > pso->sizlBitmap.cy)
    {
        h = pso->sizlBitmap.cy > y? pso->sizlBitmap.cy - y: 0;
    }

    prcl->left   = x;
    prcl->top    = y;
    prcl->right  = x + w;
    prcl->bottom = y + h;

    LOGF(("result %d-%d %d-%d", prcl->left, prcl->right, prcl->top, prcl->bottom));
}

static int vrdpGetIntersectingClipRects(VRDPCLIPRECTS *pClipRects, SURFOBJ *pso, RECTL *prcl, CLIPOBJ *pco, POINTL *pptlSrc)
{
    BOOL bTooManyRects = FALSE;

    LOGF(("pso = %p, pptlSrc = %p", pso, pptlSrc));

    pso = getSurfObj(pso);

    pClipRects->rclDstOrig = *prcl;
    pClipRects->rclDst     = *prcl;
    pClipRects->rects.c    = 0;

    vrdpAdjustRect(pso, &pClipRects->rclDst);

    if (pco && (pco->iDComplexity != DC_TRIVIAL))
    {
        ULONG iDirection = CD_ANY;

        if (pptlSrc)
        {
            /* Operation is performed on the same (screen) surface and enumeration direction
             * must take into account the position of source and target rectangles.
             */
            if (pptlSrc->x <= prcl->left)
            {
                if (pptlSrc->y <= prcl->top)
                {
                    iDirection = CD_LEFTUP;
                }
                else
                {
                    iDirection = CD_LEFTDOWN;
                }
            }
            else
            {
                if (pptlSrc->y <= prcl->top)
                {
                    iDirection = CD_RIGHTUP;
                }
                else
                {
                    iDirection = CD_RIGHTDOWN;
                }
            }
        }

        /* Clip the target rect by entire clipping region. Obtain the effective target. */
        vrdpIntersectRects(&pClipRects->rclDst, &pClipRects->rclDst, &pco->rclBounds);

        /* Enumerate rectangles. Try to get all rectangles at once and if there is not
         * enough space (too many rectangles) fail with the bTooManyRects condition.
         */
        CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, iDirection, 0);

        bTooManyRects = CLIPOBJ_bEnum(pco, sizeof(pClipRects->rects), &pClipRects->rects.c);

        if (!bTooManyRects)
        {
            RECTL *prclClipSrc = &pClipRects->rects.arcl[0];
            RECTL *prclClipDst = prclClipSrc;

            ULONG cRects = pClipRects->rects.c;

            LOGF(("%d rects", cRects));

            if (cRects > 0)
            {
                for (; cRects != 0; cRects--, prclClipSrc++)
                {
                    vrdpIntersectRects(prclClipDst, prclClipSrc, &pClipRects->rclDst);

                    if (vrdpIsRectEmpty(prclClipDst))
                    {
                        pClipRects->rects.c--;
                    }
                    else
                    {
                        prclClipDst++;
                    }
                }
            }

            if (pClipRects->rects.c == 0)
            {
                pClipRects->rclDst.left = pClipRects->rclDst.right = 0;
            }
            LOGF(("%d rects remains", pClipRects->rects.c));
        }
    }

    if (vrdpIsRectEmpty(&pClipRects->rclDst))
    {
        return VRDP_CLIP_NO_INTERSECTION;
    }

    if (bTooManyRects)
    {
        pClipRects->rects.c = 0;

        return VRDP_CLIP_TOO_MANY_RECTS;
    }

    return VRDP_CLIP_OK;
}

static void vrdpReportDirtyPathBounds(PVBOXDISPDEV pDev, CLIPOBJ *pco, PATHOBJ *ppo)
{
    RECTFX rcfxBounds;
    RECTL rclBounds;

    PATHOBJ_vGetBounds(ppo, &rcfxBounds);

    rclBounds.left   = FXTOLFLOOR(rcfxBounds.xLeft);
    rclBounds.right  = FXTOLCEILING(rcfxBounds.xRight);
    rclBounds.top    = FXTOLFLOOR(rcfxBounds.yTop);
    rclBounds.bottom = FXTOLCEILING(rcfxBounds.yBottom);

    vrdpIntersectRects(&rclBounds, &rclBounds, &pco->rclBounds);

    vrdpReportDirtyRect(pDev, &rclBounds);
}

static void vrdpReportDirtyPath(PVBOXDISPDEV pDev, CLIPOBJ *pco, PATHOBJ *ppo)
{
    vrdpReportDirtyPathBounds(pDev, pco, ppo);
}

static void vrdpReportDirtyClip(PVBOXDISPDEV pDev, CLIPOBJ *pco, RECTL *prcl)
{
    if (prcl)
    {
        vrdpReportDirtyRect(pDev, prcl);
    }
    else if (pco)
    {
        vrdpReportDirtyRect(pDev, &pco->rclBounds);
    }
}

static void vrdpReportDirtyRects(PVBOXDISPDEV pDev, VRDPCLIPRECTS *pClipRects)
{
    /* Ignore rects, report entire area. */
    vrdpReportDirtyRect (pDev, &pClipRects->rclDst);
}

__inline BOOL vrdpWriteHdr (PVBOXDISPDEV pDev, uint32_t u32Op)
{
    return VBoxVBVAWrite(&pDev->vbvaCtx, &pDev->hgsmi.ctx, &u32Op, sizeof (u32Op));
}

static BOOL vrdpWriteBits (PVBOXDISPDEV pDev, uint8_t *pu8Bits, int lDelta, int32_t x, int32_t y, uint32_t cWidth, uint32_t cHeight, int bytesPerPixel)
{
    BOOL bRc = FALSE;

    VRDEDATABITS bits;

    bits.cb      = cHeight * cWidth * bytesPerPixel;
    bits.x       = (int16_t)x;
    bits.y       = (int16_t)y;
    bits.cWidth  = (uint16_t)cWidth;
    bits.cHeight = (uint16_t)cHeight;
    bits.cbPixel = (uint8_t)bytesPerPixel;

    bRc = VBoxVBVAWrite(&pDev->vbvaCtx, &pDev->hgsmi.ctx, &bits, sizeof (bits));

    if (bRc)
    {
        while (cHeight--)
        {
            bRc = VBoxVBVAWrite(&pDev->vbvaCtx, &pDev->hgsmi.ctx, pu8Bits, cWidth * bytesPerPixel);

            if (!bRc)
            {
                break;
            }

            pu8Bits += lDelta;
        }
    }

    return bRc;
}

/*
 * RDP orders reporting.
 */
static BOOL vrdpReportOrder(PVBOXDISPDEV pDev, const void *pOrder, unsigned cbOrder, unsigned code)
{
    BOOL bRc = vrdpWriteHdr(pDev, VRDP_MAKE_OP(code));

    if (bRc)
    {
        VBoxVBVAWrite(&pDev->vbvaCtx, &pDev->hgsmi.ctx, pOrder, cbOrder);
    }

    return bRc;
}

static BOOL vrdpReportBounds(PVBOXDISPDEV pDev, const RECTL *prcl)
{
    VRDEORDERBOUNDS bounds;

    bounds.pt1.x = (int16_t)(prcl->left);
    bounds.pt1.y = (int16_t)(prcl->top);
    bounds.pt2.x = (int16_t)(prcl->right);
    bounds.pt2.y = (int16_t)(prcl->bottom);

    return vrdpReportOrder(pDev, &bounds, sizeof (bounds), VRDE_ORDER_BOUNDS);
}

static BOOL vrdpReportRepeat(PVBOXDISPDEV pDev, const CLIPRECTS *pRects)
{
    BOOL bRc = TRUE;

    if (pRects)
    {
        /* Start from index 1, because the first rect was already reported. */
        unsigned i = 1;
        const RECTL *prcl = &pRects->arcl[1];

        for (; i < pRects->c; i++, prcl++)
        {
            VRDEORDERREPEAT repeat;

            repeat.bounds.pt1.x = (int16_t)(prcl->left);
            repeat.bounds.pt1.y = (int16_t)(prcl->top);
            repeat.bounds.pt2.x = (int16_t)(prcl->right);
            repeat.bounds.pt2.y = (int16_t)(prcl->bottom);

            bRc = vrdpReportOrder(pDev, &repeat, sizeof (repeat), VRDE_ORDER_REPEAT);

            if (!bRc)
            {
                return bRc;
            }
        }
    }

    return bRc;
}

void vrdpReportDirtyRect(PVBOXDISPDEV pDev, RECTL *prcl)
{
    SURFOBJ *pso = pDev->surface.psoBitmap;

    /* This is a Bitmap Update Fallback operation. It takes bits from VRAM
     * and inserts them in the pipeline. These bits are not cached.
     */

    uint8_t *pu8Bits;
    int32_t lDelta;
    uint32_t cWidth;
    uint32_t cHeight;

    BOOL bRc = FALSE;

    int bytesPerPixel = format2BytesPerPixel(pso);

    RECTL rclCopy = *prcl;

    vrdpAdjustRect (pso, &rclCopy);

    pu8Bits = (uint8_t *)pso->pvScan0 +
                         pso->lDelta * rclCopy.top +
                         bytesPerPixel * rclCopy.left;
    lDelta  = pso->lDelta;
    cWidth  = rclCopy.right - rclCopy.left;
    cHeight = rclCopy.bottom - rclCopy.top;

    if (cWidth == 0 || cHeight == 0)
    {
        return;
    }

    if (bytesPerPixel > 0)
    {
        bRc = vrdpWriteHdr(pDev, VRDP_MAKE_OP(VRDE_ORDER_DIRTY_RECT));

        if (bRc)
        {
            bRc = vrdpWriteBits(pDev, pu8Bits, lDelta, rclCopy.left, rclCopy.top, cWidth, cHeight, bytesPerPixel);
        }
    }

    if (!bRc)
    {
        WARN(("failed!!! %d,%d %dx%d, bpp = %d\n",
              rclCopy.left, rclCopy.top, cWidth, cHeight, bytesPerPixel));
    }
}

static BOOL vrdpIntersectRectWithBounds (RECTL *prclIntersect,
                                         const RECTL *prcl,
                                         const VRDEORDERBOUNDS *pBounds)
{
    if (   prcl->left   < pBounds->pt2.x     /* left < bounds_right */
        && prcl->right  > pBounds->pt1.x     /* right < bounds_left */
        && prcl->top    < pBounds->pt2.y     /* top < bounds_bottom */
        && prcl->bottom > pBounds->pt1.y     /* bottom < bounds_top */
        )
    {
        /* There is intersection. */
        prclIntersect->left   = max(prcl->left, pBounds->pt1.x);
        prclIntersect->right  = min(prcl->right, pBounds->pt2.x);
        prclIntersect->top    = max(prcl->top, pBounds->pt1.y);
        prclIntersect->bottom = min(prcl->bottom, pBounds->pt2.y);

        Assert(prclIntersect->left < prclIntersect->right);
        Assert(prclIntersect->top < prclIntersect->bottom);

        return TRUE;
    }

    /* No intersection. */
    return FALSE;
}

static BOOL vrdpGetIntersectingRects (CLIPRECTS *pRects,
                                      const VRDPCLIPRECTS *pClipRects,
                                      const VRDEORDERBOUNDS *pBounds)
{
    BOOL fReportOrder = TRUE;

    pRects->c = 0;              /* Number of clipping rects. */

    if (pClipRects->rects.c == 0)
    {
        /* There were no clipping for the order. Therefore do nothing
         * here and just return that order must be reported without
         * clipping (rc = TRUE, pRects->c = 0).
         */
        /* Do nothing. */
    }
    else
    {
        /* Find which clipping rects intersect with the bounds. */
        unsigned c = 0;
        RECTL *prclIntersect = &pRects->arcl[0];

        unsigned i = 0;
        const RECTL *prcl = &pClipRects->rects.arcl[0];

        for (; i < pClipRects->rects.c; i++, prcl++)
        {
            if (vrdpIntersectRectWithBounds (prclIntersect, prcl, pBounds))
            {
                c++;
                prclIntersect++;
            }
        }

        if (c == 0)
        {
            /* No of clip rects intersect with the bounds. */
            fReportOrder = FALSE;
        }
        else
        {
            pRects->c = c;
        }
    }

    return fReportOrder;
}

BOOL vrdpReportOrderGeneric (PVBOXDISPDEV pDev,
                             const VRDPCLIPRECTS *pClipRects,
                             const void *pvOrder,
                             unsigned cbOrder,
                             unsigned code)
{
    BOOL bRc;

    if (pClipRects && pClipRects->rects.c > 0)
    {
        bRc = vrdpReportBounds (pDev, &pClipRects->rects.arcl[0]);

        if (!bRc)
        {
            return bRc;
        }
    }

    bRc = vrdpReportOrder (pDev, pvOrder, cbOrder, code);

    if (!bRc)
    {
        return bRc;
    }

    if (pClipRects && pClipRects->rects.c > 1)
    {
        bRc = vrdpReportRepeat (pDev, &pClipRects->rects);
    }

    return bRc;
}

static void vrdpReportOrderGenericBounds (PVBOXDISPDEV pDev,
                                          const VRDPCLIPRECTS *pClipRects,
                                          const VRDEORDERBOUNDS *pBounds,
                                          const void *pvOrder,
                                          unsigned cbOrder,
                                          unsigned code)
{
    CLIPRECTS rects;

    if (vrdpGetIntersectingRects (&rects, pClipRects, pBounds))
    {
        vrdpReportOrderGeneric (pDev, pClipRects, pvOrder, cbOrder, code);
    }
}

static void vrdpReportSolidRect (PVBOXDISPDEV pDev,
                                 const RECTL *prclTrg,
                                 VRDPCLIPRECTS *pClipRects,
                                 ULONG rgb)
{
    VRDEORDERSOLIDRECT order;

    order.x     = (int16_t)prclTrg->left;
    order.y     = (int16_t)prclTrg->top;
    order.w     = (uint16_t)(prclTrg->right - prclTrg->left);
    order.h     = (uint16_t)(prclTrg->bottom - prclTrg->top);
    order.rgb   = rgb;

    vrdpReportOrderGeneric (pDev, pClipRects, &order, sizeof (order), VRDE_ORDER_SOLIDRECT);
}

static void vrdpReportSolidBlt (PVBOXDISPDEV pDev,
                                const RECTL *prclTrg,
                                VRDPCLIPRECTS *pClipRects,
                                ULONG rgb,
                                uint8_t rop3)
{
    VRDEORDERSOLIDBLT order;

    order.x     = (int16_t)prclTrg->left;
    order.y     = (int16_t)prclTrg->top;
    order.w     = (uint16_t)(prclTrg->right - prclTrg->left);
    order.h     = (uint16_t)(prclTrg->bottom - prclTrg->top);
    order.rgb   = rgb;
    order.rop   = rop3;

    vrdpReportOrderGeneric (pDev, pClipRects, &order, sizeof (order), VRDE_ORDER_SOLIDBLT);
}

static void vrdpReportPatBlt (PVBOXDISPDEV pDev,
                              const RECTL *prclTrg,
                              VRDPCLIPRECTS *pClipRects,
                              VRDPBRUSH *pBrush,
                              POINTL *pptlBrush,
                              uint8_t rop3)
{
    VRDEORDERPATBLTBRUSH order;

    int8_t xSrc = 0;
    int8_t ySrc = 0;

    if (pptlBrush)
    {
        LOG(("Dst %d,%d Brush origin %d,%d", prclTrg->left, prclTrg->top, pptlBrush->x, pptlBrush->y));

        /* Make sure that the coords fit in a 8 bit value.
         * Only 8x8 pixel brushes are supported, so last 3 bits
         * is a [0..7] coordinate of the brush, because the brush
         * repeats after each 8 pixels.
         */
        xSrc = (int8_t)(pptlBrush->x & 7);
        ySrc = (int8_t)(pptlBrush->y & 7);
    }

    order.x     = (int16_t)prclTrg->left;
    order.y     = (int16_t)prclTrg->top;
    order.w     = (uint16_t)(prclTrg->right - prclTrg->left);
    order.h     = (uint16_t)(prclTrg->bottom - prclTrg->top);
    order.xSrc  = xSrc;
    order.ySrc  = ySrc;
    order.rgbFG = pBrush->u.pat.rgbFG;
    order.rgbBG = pBrush->u.pat.rgbBG;
    order.rop   = rop3;

    memcpy (order.pattern, pBrush->u.pat.au8Pattern, sizeof (order.pattern));

    vrdpReportOrderGeneric (pDev, pClipRects, &order, sizeof (order), VRDE_ORDER_PATBLTBRUSH);
}

static void vrdpReportDstBlt (PVBOXDISPDEV pDev,
                              const RECTL *prclTrg,
                              VRDPCLIPRECTS *pClipRects,
                              uint8_t rop3)
{
    VRDEORDERDSTBLT order;

    order.x     = (int16_t)prclTrg->left;
    order.y     = (int16_t)prclTrg->top;
    order.w     = (uint16_t)(prclTrg->right - prclTrg->left);
    order.h     = (uint16_t)(prclTrg->bottom - prclTrg->top);
    order.rop   = rop3;

    vrdpReportOrderGeneric (pDev, pClipRects, &order, sizeof (order), VRDE_ORDER_DSTBLT);
}

static void vrdpReportScreenBlt (PVBOXDISPDEV pDev,
                                 const RECTL *prclTrg,
                                 VRDPCLIPRECTS *pClipRects,
                                 POINTL *pptlSrc,
                                 uint8_t rop3)
{
    VRDEORDERSCREENBLT order;

    order.x     = (int16_t)prclTrg->left;
    order.y     = (int16_t)prclTrg->top;
    order.w     = (uint16_t)(prclTrg->right - prclTrg->left);
    order.h     = (uint16_t)(prclTrg->bottom - prclTrg->top);
    order.xSrc  = (int16_t)pptlSrc->x;
    order.ySrc  = (int16_t)pptlSrc->y;
    order.rop   = rop3;

    vrdpReportOrderGeneric (pDev, pClipRects, &order, sizeof (order), VRDE_ORDER_SCREENBLT);
}

static void vrdpReportMemBltRect (PVBOXDISPDEV pDev,
                                  RECTL *prcl,
                                  int xSrc,
                                  int ySrc,
                                  uint8_t rop3,
                                  const VRDPBCHASH *phash)
{
    VRDEORDERMEMBLT order;

    order.x    = (int16_t)prcl->left;
    order.y    = (int16_t)prcl->top;
    order.w    = (uint16_t)(prcl->right - prcl->left);
    order.h    = (uint16_t)(prcl->bottom - prcl->top);
    order.xSrc = (int16_t)xSrc;
    order.ySrc = (int16_t)ySrc;
    order.rop  = rop3;

    Assert(sizeof (*phash) == sizeof (order.hash));
    memcpy (order.hash, phash, sizeof (*phash));

    vrdpReportOrder (pDev, &order, sizeof (order), VRDE_ORDER_MEMBLT);
}

static void vrdpReportMemBlt (PVBOXDISPDEV pDev,
                              VRDPCLIPRECTS *pClipRects,
                              POINTL *pptlSrc,
                              const uint8_t rop3,
                              const VRDPBCHASH *phash)
{
    if (pClipRects->rects.c == 0)
    {
        int xShift = pClipRects->rclDst.left - pClipRects->rclDstOrig.left;
        int yShift = pClipRects->rclDst.top - pClipRects->rclDstOrig.top;

        Assert(xShift >= 0 && yShift >= 0);

        vrdpReportMemBltRect (pDev, &pClipRects->rclDst, pptlSrc->x + xShift, pptlSrc->y + yShift, rop3, phash);
    }
    else
    {
        ULONG i;
        for (i = 0; i < pClipRects->rects.c; i++)
        {
            int xShift = pClipRects->rects.arcl[i].left - pClipRects->rclDstOrig.left;
            int yShift = pClipRects->rects.arcl[i].top - pClipRects->rclDstOrig.top;

            Assert(xShift >= 0 && yShift >= 0);

            vrdpReportMemBltRect (pDev, &pClipRects->rects.arcl[i], pptlSrc->x + xShift, pptlSrc->y + yShift, rop3, phash);
        }
    }
}

static void vrdpReportCachedBitmap (PVBOXDISPDEV pDev,
                                    SURFOBJ *psoSrc,
                                    const VRDPBCHASH *phash)
{
    BOOL bRc;

    VRDEORDERCACHEDBITMAP order;

    Assert(sizeof (*phash) == sizeof (order.hash));
    memcpy (order.hash, phash, sizeof (*phash));

    bRc = vrdpReportOrder (pDev, &order, sizeof (order), VRDE_ORDER_CACHED_BITMAP);

    if (bRc)
    {
        int bytesPerPixel = format2BytesPerPixel(psoSrc);

        uint8_t *pu8Bits = (uint8_t *)psoSrc->pvScan0;
        int32_t lDelta   = psoSrc->lDelta;
        uint32_t cWidth  = psoSrc->sizlBitmap.cx;
        uint32_t cHeight = psoSrc->sizlBitmap.cy;

        Assert(cWidth != 0 && cHeight != 0 && bytesPerPixel != 0);

        vrdpWriteBits (pDev, pu8Bits, lDelta, 0, 0, cWidth, cHeight, bytesPerPixel);
    }
}

static void vrdpReportDeletedBitmap (PVBOXDISPDEV pDev,
                                     const VRDPBCHASH *phash)
{
    VRDEORDERDELETEDBITMAP order;

    Assert(sizeof (*phash) == sizeof (order.hash));
    memcpy (order.hash, phash, sizeof (*phash));

    vrdpReportOrder (pDev, &order, sizeof (order), VRDE_ORDER_DELETED_BITMAP);
}


void vrdpReset(PVBOXDISPDEV pDev)
{
    LOGF(("%p", pDev));

    vrdpbmpReset(&pDev->vrdpCache);

    return;
}

/*
 * VRDP driver functions.
 */

void vrdpDrvLineTo(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo,
                   LONG x1, LONG y1, LONG x2, LONG y2, RECTL *prclBounds, MIX mix)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;

    /*
     * LineTo operation is supported by RDP_ORDER_LINE.
     */
    VRDPCLIPRECTS clipRects;
    int clipResult;
    RECTL rclBoundsOrdered = *prclBounds;

    vrdpOrderRect(&rclBoundsOrdered);

    clipResult = vrdpGetIntersectingClipRects(&clipRects, pso, &rclBoundsOrdered, pco, NULL);

    if (clipResult == VRDP_CLIP_NO_INTERSECTION)
    {
        /* Do nothing. The Blt does not affect anything. */
        LOG(("VRDP_CLIP_NO_INTERSECTION!!!"));
        dumpPCO(&rclBoundsOrdered, pco);
    }
    else if (clipResult == VRDP_CLIP_TOO_MANY_RECTS)
    {
        /* A very complex clip. Better to emulate it. */
        LOG(("VRDP_CLIP_TOO_MANY_RECTS!!!"));
        dumpPCO(&rclBoundsOrdered, pco);

        vrdpReportDirtyRects(pDev, &clipRects);
    }
    else if (pbo->iSolidColor == 0xFFFFFFFF)
    {
        /* Not solid brushes are not supported. */
        vrdpReportDirtyRects(pDev, &clipRects);
    }
    else
    {
        VRDEORDERLINE order;

        order.x1 = (int16_t)x1;
        order.y1 = (int16_t)y1;
        order.x2 = (int16_t)x2;
        order.y2 = (int16_t)y2;

        order.xBounds1 = ~0;
        order.yBounds1 = ~0;
        order.xBounds2 = ~0;
        order.yBounds2 = ~0;

        order.mix = (uint8_t)(mix & 0x1F);
        order.rgb = vrdpColor2RGB (pso, pbo->iSolidColor);

        LOG(("LINE %d,%d to %d,%d mix %02X rgb %08X bounds %d-%d %d-%d cliprects %d.",
             x1, y1, x2, y2, order.mix, order.rgb,
             prclBounds->left, prclBounds->right, prclBounds->top, prclBounds->bottom, clipRects.rects.c));

        vrdpReportOrderGeneric(pDev, &clipRects, &order, sizeof (order), VRDE_ORDER_LINE);
    }
}

void vrdpDrvStrokePath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, XFORMOBJ *pxo,
                       BRUSHOBJ  *pbo, POINTL *pptlBrushOrg, LINEATTRS *plineattrs, MIX mix)
{
    RT_NOREF(pxo,  pptlBrushOrg);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;

    /*
     * StrokePath operation is supported by RDP_ORDER_POLYGON/POLYLINE/ELLIPSE.
     */
    VRDPCLIPRECTS clipRects;
    int clipResult;
    RECTFX rcfxBounds;
    RECTL rclBoundsOrdered;

    LOGF(("pso = %p, ppo = %p, pco = %p, pxo = %p, pbo = %p, pptlBrushOrg = %p, plineattrs = %p, mix = 0x%08X",
           pso, ppo, pco, pxo, pbo, pptlBrushOrg, plineattrs, mix));
    LOGF(("ppo: fl = 0x%08X, cCurves = %d", ppo->fl, ppo->cCurves));

    PATHOBJ_vGetBounds(ppo, &rcfxBounds);

    rclBoundsOrdered.left   = FXTOLFLOOR(rcfxBounds.xLeft);
    rclBoundsOrdered.right  = FXTOLCEILING(rcfxBounds.xRight);
    rclBoundsOrdered.top    = FXTOLFLOOR(rcfxBounds.yTop);
    rclBoundsOrdered.bottom = FXTOLCEILING(rcfxBounds.yBottom);

    vrdpOrderRect(&rclBoundsOrdered);

    LOG(("ppo: bounds %x-%x, %x-%x, %d-%d %d-%d",
         rcfxBounds.xLeft, rcfxBounds.xRight, rcfxBounds.yTop, rcfxBounds.yBottom,
         rclBoundsOrdered.left, rclBoundsOrdered.right, rclBoundsOrdered.top, rclBoundsOrdered.bottom));

    clipResult = vrdpGetIntersectingClipRects(&clipRects, pso, &rclBoundsOrdered, pco, NULL);

    if (clipResult == VRDP_CLIP_NO_INTERSECTION)
    {
        /* Do nothing. The operation does not affect anything. */
        LOG(("VRDP_CLIP_NO_INTERSECTION!!!"));
        dumpPCO (&rclBoundsOrdered, pco);
    }
    else if (clipResult == VRDP_CLIP_TOO_MANY_RECTS)
    {
        /* A very complex clip. Better to emulate it. */
        LOG(("VRDP_CLIP_TOO_MANY_RECTS!!!"));
        dumpPCO (&rclBoundsOrdered, pco);

        vrdpReportDirtyRects(pDev, &clipRects);
    }
    else if (pbo->iSolidColor == 0xFFFFFFFF)
    {
        /* Not solid brushes are not supported. */
        vrdpReportDirtyRects(pDev, &clipRects);
    }
    else if (ppo->fl & PO_ELLIPSE)
    {
        if (VBoxVBVAOrderSupported(&pDev->vbvaCtx, VRDE_ORDER_ELLIPSE))
        {
            VRDEORDERELLIPSE order;

            order.pt1.x = (int16_t)FXTOLROUND(rcfxBounds.xLeft   + 4);
            order.pt1.y = (int16_t)FXTOLROUND(rcfxBounds.yTop    + 4);
            order.pt2.x = (int16_t)FXTOLROUND(rcfxBounds.xRight  - 4);
            order.pt2.y = (int16_t)FXTOLROUND(rcfxBounds.yBottom - 4);

            order.mix = (uint8_t)(mix & 0x1F);
            order.fillMode = 0;
            order.rgb = vrdpColor2RGB(pso, pbo->iSolidColor);

            vrdpReportOrderGeneric(pDev, &clipRects, &order, sizeof (order), VRDE_ORDER_ELLIPSE);
        }
        else
        {
            WARN(("ELLIPSE not supported"));
            vrdpReportDirtyRects (pDev, &clipRects);
        }
    }
    else if (   (ppo->fl & PO_BEZIERS) == 0
             && (plineattrs->fl & LA_GEOMETRIC) == 0
             && plineattrs->pstyle == NULL)
    {
        unsigned i;
        PATHDATA pd;
        BOOL bMore;
        VRDEORDERPOLYLINE order;
        /** @todo it is not immediately obvious how we're sure ptStart isn't used uninitialized. */
        VRDEORDERPOINT ptStart = { 0, 0 }; /* Shut up MSC */
        VRDEORDERBOUNDS bounds;

        order.rgb = vrdpColor2RGB(pso, pbo->iSolidColor);
        order.mix = (uint8_t)(mix & 0x1F);

        PATHOBJ_vEnumStart(ppo);

        order.points.c = 0;

        do {
            POINTFIX *pptfx;
            /** @todo it is not immediately obvious how we're sure pt isn't used uninitialized. */
            VRDEORDERPOINT pt = {0, 0}; /* Shut up MSC */

            bMore = PATHOBJ_bEnum (ppo, &pd);

            LOG(("pd: flags = 0x%08X, count = %d", pd.flags, pd.count));

            pptfx = &pd.pptfx[0];

            if (pd.flags & PD_BEGINSUBPATH)
            {
                /* Setup first point. Start a new order. */
                LOG(("BEGINSUBPATH"));

                Assert(order.points.c == 0);

                vrdpPointFX2Point(pptfx, &ptStart);
                order.ptStart = ptStart;
                pt = ptStart;

                bounds.pt1 = bounds.pt2 = ptStart;

                pptfx++;
                i = 1;
            }
            else
            {
                LOG(("Continue order"));

                i = 0;
            }

            for (; i < pd.count; i++, pptfx++)
            {
                LOG(("pd: %2d: %x,%x %d,%d",
                     i, pptfx->x, pptfx->y, FXTOLROUND(pptfx->x), FXTOLROUND(pptfx->y)));

                vrdpPointFX2Point     (pptfx, &pt);
                vrdpPolyPointsAdd     (&order.points, &pt);
                vrdpExtendOrderBounds (&bounds, &pt);

                if (order.points.c == RT_ELEMENTS(order.points.a))
                {
                    /* Flush the order and start a new order. */
                    LOG(("Report order, points overflow."));

                    vrdpReportOrderGenericBounds(pDev, &clipRects, &bounds, &order, sizeof (order), VRDE_ORDER_POLYLINE);

                    order.points.c = 0;
                    order.ptStart = pt;
                    bounds.pt1 = bounds.pt2 = pt;
                }
            }

            if (pd.flags & PD_CLOSEFIGURE)
            {
                /* Encode the start point as the end point. */
                LOG(("Report order, CLOSEFIGURE"));

                if (   ptStart.x != pt.x
                    || ptStart.y != pt.y)
                {
                    Assert(order.points.c < RT_ELEMENTS(order.points.a));

                    vrdpPolyPointsAdd     (&order.points, &ptStart);
                    vrdpExtendOrderBounds (&bounds, &ptStart);
                }
            }

            if (pd.flags & PD_ENDSUBPATH)
            {
                /* Finish the order. */
                LOG(("Report order, ENDSUBPATH"));

                if (order.points.c > 0)
                {
                    vrdpReportOrderGenericBounds(pDev, &clipRects, &bounds, &order, sizeof (order), VRDE_ORDER_POLYLINE);
                }

                order.points.c = 0;
            }
        } while (bMore);
    }
    else
    {
        /* Not supported. */
        WARN(("not supported: ppo->fl = %08X, plineattrs->fl = %08X, plineattrs->pstyle = %08X",
              ppo->fl, plineattrs->fl, plineattrs->pstyle));

        vrdpReportDirtyRects(pDev, &clipRects);
    }

    return;
}

void vrdpDrvFillPath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix, FLONG flOptions)
{
    RT_NOREF(pbo, pptlBrushOrg, mix, flOptions);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    vrdpReportDirtyPath(pDev, pco, ppo);
}

void vrdpDrvPaint(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix)
{
    RT_NOREF(pbo, pptlBrushOrg, mix);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    vrdpReportDirtyClip(pDev, pco, NULL);
}

void vrdpDrvTextOut(SURFOBJ *pso, STROBJ *pstro, FONTOBJ *pfo, CLIPOBJ *pco, RECTL *prclExtra, RECTL *prclOpaque,
                    BRUSHOBJ *pboFore, BRUSHOBJ *pboOpaque, POINTL *pptlOrg, MIX mix)
{
    RT_NOREF(pptlOrg,  mix);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;

    /*
     * TextOut operation is supported by RDP_ORDER_TEXT2/FONTCACHE.
     */
    VRDPCLIPRECTS clipRects;
    int clipResult;

    RECTL rclArea = prclOpaque? *prclOpaque: pstro->rclBkGround;

    clipResult = vrdpGetIntersectingClipRects(&clipRects, pso, &rclArea, pco, NULL);

    if (clipResult == VRDP_CLIP_NO_INTERSECTION)
    {
        /* Do nothing. The operation does not affect anything. */
        LOG(("VRDP_CLIP_NO_INTERSECTION!!!"));
        dumpPCO (&rclArea, pco);
    }
    else if (clipResult == VRDP_CLIP_TOO_MANY_RECTS)
    {
        /* A very complex clip. Better to emulate it. */
        LOG(("VRDP_CLIP_TOO_MANY_RECTS!!!"));
        dumpPCO (&rclArea, pco);

        vrdpReportDirtyRects (pDev, &clipRects);
    }
    else if (   pstro->pwszOrg == NULL
             || prclExtra != NULL
             || (pfo->flFontType & FO_TYPE_RASTER) == 0
             || pstro->cGlyphs > VRDP_TEXT_MAX_GLYPHS
             || (pboOpaque && pboOpaque->iSolidColor == 0xFFFFFFFF)
             || pfo->iUniq == 0
            )
    {
        /* Unknown/unsupported parameters. */
        WARN(("unsupported: pstro->pwszOrg=%p, prclExtra=%p, pfo->flFontType & FO_TYPE_RASTER = 0x%08X, "
              "pstro->cGlyphs = %d, pboOpaque->iSolidColor %p, pfo->iUniq = %p",
              pstro->pwszOrg, prclExtra, pfo->flFontType & FO_TYPE_RASTER, pstro->cGlyphs,
              pboOpaque? pboOpaque->iSolidColor: 0, pfo->iUniq));
        vrdpReportDirtyRects(pDev, &clipRects);
    }
    else
    {
#if 0
        /* Testing: report a red rectangle for the text area. */
        vrdpReportSolidRect(pDev, &clipRects, 0x0000FF);
#else
        /* Try to report the text order. */
        ULONG ulForeRGB = pboFore? vrdpColor2RGB(pso, pboFore->iSolidColor): 0;
        ULONG ulBackRGB = pboOpaque? vrdpColor2RGB(pso, pboOpaque->iSolidColor): 0;

        LOG(("calling vboxReportText fg %x bg %x", ulForeRGB, ulBackRGB));

        if (!vrdpReportText(pDev, &clipRects, pstro, pfo, prclOpaque, ulForeRGB, ulBackRGB))
        {
            vrdpReportDirtyRects(pDev, &clipRects);
        }
#endif
    }

    return;
}

void vrdpDrvSaveScreenBits(SURFOBJ *pso, ULONG iMode, ULONG_PTR ident, RECTL *prcl)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;

    switch (iMode)
    {
        case SS_SAVE:
        {
            VRDEORDERSAVESCREEN order;

            order.pt1.x = (int16_t)prcl->left;
            order.pt1.y = (int16_t)prcl->top;
            order.pt2.x = (int16_t)prcl->right;
            order.pt2.y = (int16_t)prcl->bottom;

            order.ident = (uint8_t)ident;
            order.restore = 0;

            vrdpReportOrderGeneric(pDev, NULL, &order, sizeof (order), VRDE_ORDER_SAVESCREEN);
        } break;

        case SS_RESTORE:
        {
            VRDEORDERSAVESCREEN order;

            order.pt1.x = (int16_t)prcl->left;
            order.pt1.y = (int16_t)prcl->top;
            order.pt2.x = (int16_t)prcl->right;
            order.pt2.y = (int16_t)prcl->bottom;

            order.ident = (uint8_t)ident;
            order.restore = 1;

            if (vrdpReportOrderGeneric(pDev, NULL, &order, sizeof (order), VRDE_ORDER_SAVESCREEN))
            {
                uint8_t *pu8Bits;
                int32_t lDelta;
                uint32_t w;
                uint32_t h;

                int cbPixel;

                pso = getSurfObj(pso);

                cbPixel = format2BytesPerPixel(pso);

                pu8Bits = (uint8_t *)pso->pvScan0 +
                                     pso->lDelta * prcl->top +
                                     cbPixel * prcl->left;

                lDelta  = pso->lDelta;

                w = prcl->right - prcl->left;
                h = prcl->bottom - prcl->top;

                vrdpWriteBits(pDev, pu8Bits, lDelta, prcl->left, prcl->top, w, h, cbPixel);
            }
        } break;

        default:
           WARN(("Invalid mode %d!!!", iMode));
    }
}

/* Whether the ROP4 operation requires MASK. */
#define ROP4_NEED_MASK(__rop4) ( (uint8_t)((__rop4) >> 8) != (uint8_t)(__rop4) )

/* Whether the ROP3 (lower byte of rop4) operation requires BRUSH. */
#define ROP3_NEED_BRUSH(__rop3) (((((__rop3) >> 4) ^ (__rop3)) & 0x0F) != 0)

/* Whether the ROP3 (lower byte of rop4) operation requires SOURCE. */
#define ROP3_NEED_SRC(__rop3)   (((((__rop3) >> 2) ^ (__rop3)) & 0x33) != 0)

/* Whether the ROP3 (lower byte of rop4) operation requires DESTINATION. */
#define ROP3_NEED_DST(__rop3)   (((((__rop3) >> 1) ^ (__rop3)) & 0x55) != 0)

void vrdpDrvBitBlt(SURFOBJ *psoTrg, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                   RECTL *prclTrg, POINTL *pptlSrc, POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush, ROP4 rop4)
{
    RT_NOREF(psoMask, pxlo, pptlMask);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)psoTrg->dhpdev;

    /*
     * BitBlt operation is supported by following RDP orders:
     *   RDP_ORDER_DESTBLT   ROP on the screen bits (BLACKNESS, WHITENESS, DSTINVERT).
     *   RDP_ORDER_PATBLT    ROP with screen bits and a brush.
     *   RDP_ORDER_SCREENBLT Screen to screen with ROP.
     *   RDP_ORDER_RECT      Solid fill (SRCCOPY).
     *   RDP_ORDER_MEMBLT    ROP with screen and cached offscreen bitmap.
     *   RDP_ORDER_TRIBLT    ROP with screen, cached offscreen bitmap and a brush.
     *
     * Actual BitBlts must be mapped to these RDP operations.
     * Anything that can not be mapped must be emulated with dirty rect.
     *
     */
    VRDPCLIPRECTS clipRects;

    int clipResult;

    RECTL rclTrg = *prclTrg;
    vrdpOrderRect (&rclTrg);

    LOGF_ENTER();

    clipResult = vrdpGetIntersectingClipRects(&clipRects, psoTrg, &rclTrg, pco,
                                              VBoxDispIsScreenSurface(psoSrc)? pptlSrc: NULL);

    if (clipResult == VRDP_CLIP_NO_INTERSECTION)
    {
        /* Do nothing. The Blt does not affect anything. */
        WARN(("VRDP_CLIP_NO_INTERSECTION!!!"));
        dumpPCO (&rclTrg, pco);
    }
    else if (clipResult == VRDP_CLIP_TOO_MANY_RECTS)
    {
        /* A very complex clip. Better to emulate it. */
        WARN(("VRDP_CLIP_TOO_MANY_RECTS!!!"));
        dumpPCO (&rclTrg, pco);

        vrdpReportDirtyRects(pDev, &clipRects);
    }
    else if (ROP4_NEED_MASK (rop4))
    {
        /* Operation with mask is not supported. */
        WARN(("Operation with mask is not supported."));
        vrdpReportDirtyRects(pDev, &clipRects);
    }
    else if (ROP3_NEED_BRUSH(rop4))
    {
        LOG(("Operation requires brush."));

        /* Operation requires brush. */

        if (ROP3_NEED_SRC(rop4))
        {
            /** @todo Three way blt. RDP_ORDER_TRIBLT. */
            LOG(("TRIBLT pbo->iSolidColor = 0x%08X.", pbo->iSolidColor));
            vrdpReportDirtyRects(pDev, &clipRects);
        }
        else
        {
            /* Only brush and destination. Check if the brush is solid. */
            if (pbo->iSolidColor != 0xFFFFFFFF)
            {
                /* Solid brush. The iSolidColor is the target surface color. */
                uint32_t rgb = vrdpColor2RGB(psoTrg, pbo->iSolidColor);

                /* Mix with solid brush. RDP_ORDER_PATBLT. Or RDP_ORDER_RECT for rop4 = 0xF0F0. */
                LOG(("Solid PATBLT color = %08X, rgb %08X.", pbo->iSolidColor, rgb));

                if (rop4 == 0xF0F0)
                {
                    vrdpReportSolidRect(pDev, &rclTrg, &clipRects, rgb);
                }
                else
                {
                    vrdpReportSolidBlt(pDev, &rclTrg, &clipRects, rgb, (uint8_t)rop4);
                }
            }
            else
            {
                /* Non solid brush. RDP_ORDER_PATBLT. */
                LOG(("VRDP::vrdpBitBlt: PATBLT pbo->pvRbrush = %p.", pbo->pvRbrush));

                /* Realize brush. */
                if (!pbo->pvRbrush)
                {
                    BRUSHOBJ_pvGetRbrush (pbo);
                }

                if (pbo->pvRbrush)
                {
                    /* Brush has been realized. */
                    VRDPBRUSH *pBrush = (VRDPBRUSH *)pbo->pvRbrush;

                    if (pBrush->fPattern)
                    {
                        vrdpReportPatBlt(pDev, &rclTrg, &clipRects, pBrush, pptlBrush, (uint8_t)rop4);
                    }
                    else
                    {
                        /** @todo BITMAPCACHE followed by MEMBLT? */
                        vrdpReportDirtyRects(pDev, &clipRects);
                    }
                }
                else
                {
                    /* Unsupported brush format. Fallback to dirty rects. */
                    vrdpReportDirtyRects(pDev, &clipRects);
                }
            }
        }
    }
    else
    {
        /* Operation does not require brush. */
        if (ROP3_NEED_SRC(rop4))
        {
            LOG(("MEMBLT or SCREENBLT."));

            /* MEMBLT or SCREENBLT. */
            if (VBoxDispIsScreenSurface(psoSrc))
            {
                /* Screen to screen transfer. SCREENBLT. */
                LOG(("SCREENBLT."));
                vrdpReportScreenBlt(pDev, &rclTrg, &clipRects, pptlSrc, (uint8_t)rop4);
            }
            else
            {
                /* Offscreen bitmap to screen. MEMBLT. */
                VRDPBCHASH hash;
                VRDPBCHASH hashDeleted;
                int cacheResult;

                LOG(("MEMBLT: bitmap %dx%d.", psoSrc->sizlBitmap.cx, psoSrc->sizlBitmap.cy));
                if (   pDev->bBitmapCacheDisabled
                    || (psoSrc->fjBitmap & BMF_DONTCACHE) != 0
                    || psoSrc->iUniq == 0
                       /* Bitmaps with hdev == 0 seems to have different RGB layout for 16BPP modes.
                        * Just do not cache these bitmaps and report the dirty display area instead.
                        */
                    || (   psoSrc->hdev == 0
                        && !(psoSrc->iBitmapFormat == BMF_24BPP || psoSrc->iBitmapFormat == BMF_32BPP)
                       )
                       /* Do not try to cache large bitmaps. The cache should be mostly used for icons, etc.
                        * Computing a bitmap hash increases CPU load. Up to 384K pixels (~620x620)
                        */
                    || psoSrc->sizlBitmap.cx * psoSrc->sizlBitmap.cy > 384 * _1K
                   )
                {
                    LOG(("MEMBLT: non cacheable bitmap."));
                    cacheResult = VRDPBMP_RC_NOT_CACHED;
                }
                else
                {
                    LOG(("MEMBLT: going to cache."));
                    cacheResult = vrdpbmpCacheSurface(&pDev->vrdpCache, psoSrc, &hash, &hashDeleted, FALSE);
                }

                LOG(("MEMBLT: cacheResult 0x%08X", cacheResult));

                if (cacheResult & VRDPBMP_RC_F_DELETED)
                {
                    LOG(("VRDPBMP_RC_F_DELETED"));
                    vrdpReportDeletedBitmap(pDev, &hashDeleted);
                    cacheResult &= ~VRDPBMP_RC_F_DELETED;
                }

                switch (cacheResult)
                {
                    case VRDPBMP_RC_CACHED:
                        vrdpReportCachedBitmap(pDev, psoSrc, &hash);
                        LOG(("MEMBLT: cached add %dx%d",
                             psoSrc->sizlBitmap.cx, psoSrc->sizlBitmap.cy));
                        /* Continue and report MEMBLT order. */

                    case VRDPBMP_RC_ALREADY_CACHED:
                        vrdpReportMemBlt(pDev, &clipRects, pptlSrc, (uint8_t)rop4, &hash);
                        LOG(("MEMBLT: cached use %dx%d from %d,%d %dx%d",
                             psoSrc->sizlBitmap.cx, psoSrc->sizlBitmap.cy,
                             pptlSrc->x, pptlSrc->y,
                             rclTrg.right - rclTrg.left,
                             rclTrg.bottom - rclTrg.top));
                        LOG(("        %08X %08X %08X %08X",
                                 *(uint32_t *)&((uint8_t *)&hash)[0],
                                 *(uint32_t *)&((uint8_t *)&hash)[4],
                                 *(uint32_t *)&((uint8_t *)&hash)[8],
                                 *(uint32_t *)&((uint8_t *)&hash)[12]
                               ));
                        break;

                    default:
                        /* The surface was not cached. Fallback to dirty rects. */
                        LOG(("MEMBLT: not cached %dx%d from %d,%d %dx%d",
                             psoSrc->sizlBitmap.cx, psoSrc->sizlBitmap.cy,
                             pptlSrc->x, pptlSrc->y,
                             rclTrg.right - rclTrg.left,
                             rclTrg.bottom - rclTrg.top));
                        VBoxDispDumpPSO(psoSrc, "psoSrc");
                        vrdpReportDirtyRects(pDev, &clipRects);
                }
            }
        }
        else
        {
            /* No source and no brush, only dest affected. DESTBLT. */
            LOG(("DSTBLT with rop 0x%08X", rop4));
            vrdpReportDstBlt(pDev, &rclTrg, &clipRects, (uint8_t)rop4);
        }
    }
}

void vrdpDrvStretchBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                       COLORADJUSTMENT *pca, POINTL *pptlHTOrg, RECTL *prclDest, RECTL *prclSrc, POINTL *pptlMask, ULONG iMode)
{
    RT_NOREF(psoSrc, psoMask, pxlo, pca, pptlHTOrg, prclSrc, pptlMask, iMode);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)psoDest->dhpdev;
    vrdpReportDirtyClip(pDev, pco, prclDest);
}

void vrdpDrvCopyBits(SURFOBJ *psoDest, SURFOBJ *psoSrc, CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclDest, POINTL *pptlSrc)
{
    /* The copy bits is the same as bit blt with particular set of parameters. */
    vrdpDrvBitBlt(psoDest, psoSrc, NULL, pco, pxlo, prclDest, pptlSrc, NULL, NULL, NULL, 0xCCCC);
}

BOOL vrdpDrvRealizeBrush(BRUSHOBJ *pbo, SURFOBJ *psoTarget, SURFOBJ *psoPattern, SURFOBJ *psoMask, XLATEOBJ *pxlo, ULONG iHatch)
{
    RT_NOREF(psoMask, iHatch);
    BOOL bRc = FALSE;

    LOGF(("psoMask = %p, iHatch = %d", psoMask, iHatch));
    VBoxDispDumpPSO(psoPattern, "psoPattern");

    if (psoPattern
        && psoPattern->sizlBitmap.cx == 8
        && psoPattern->sizlBitmap.cy == 8
        && psoPattern->iBitmapFormat == 1
       )
    {
        uint32_t cbBrush = sizeof (VRDPBRUSH);

        VRDPBRUSH *pBrush = (VRDPBRUSH *)BRUSHOBJ_pvAllocRbrush (pbo, cbBrush);

        LOG(("pattern pBrush = %p, size = %d", pBrush, cbBrush));

        if (pBrush)
        {
            int i;
            uint8_t *pu8Bits = (uint8_t *)psoPattern->pvScan0;

            for (i = 0; i < 8; i++)
            {
                pBrush->u.pat.au8Pattern[i] = *pu8Bits;

                pu8Bits += psoPattern->lDelta;
            }

            /* Obtain RGB values for the brush fore and background colors:
             * "should translate color zero through the XLATEOBJ to get the foreground color for the brush."
             */
            pBrush->u.pat.rgbFG = vrdpColor2RGB (psoTarget, pxlo->pulXlate[0]);
            pBrush->u.pat.rgbBG = vrdpColor2RGB (psoTarget, pxlo->pulXlate[1]);

            pBrush->fPattern = TRUE;

            bRc = TRUE;
        }
    }
#if 0
    else if (psoPattern)
    {
        /* Color brushes and brushes >8x8 are cached and MEMBLT order generated. */
        uint32_t cbBrush = sizeof (VRDPBRUSH) +
                           psoTarget->sizlBitmap.cx * sizeof (uint32_t) * psoTarget->sizlBitmap.cy;
                           ??? target

        VRDPBRUSH *pBrush = (VRDPBRUSH *)BRUSHOBJ_pvAllocRbrush (pbo, cbBrush);

        LOG(("bitmap pBrush = %p, size = %d", pBrush, cbBrush));

        if (pBrush)
        {
            /* Byte per pattern pixel. */
            uint32_t cbSrcBPP = format2BytesPerPixel(psoPattern);

            /* Source bits scanline pointer. */
            uint8_t  *pu8BitsSrcScanLine = (uint8_t *)psoPattern->pvScan0;

            /* Target RGB pixel pointer. */
            uint32_t *pu32BitsDst = &pBrush->u.bitmap.au32Bits[0];

            int y;
            for (y = 0; y < psoTarget->sizlBitmap.cy; y++, pu8BitsSrcScanLine += psoPattern->lDelta)
            {
                uint8_t *pu8BitsSrc = pu8BitsSrcScanLine;

                int x;

                for (x = 0; x < psoTarget->sizlBitmap.cx; x++, pu8BitsSrc += cbSrcBPP)
                {
                    uint32_t color = 0;

                    memcpy (&color, pu8BitsSrc, cbSrcBPP);

                    if (pxlo)
                    {
                        color = XLATEOBJ_iXlate (pxlo, color);
                    }

                    *pu32BitsDst++ = vrdpColor2RGB (psoTarget, color);

                    /* LOG(("%08X", pu32BitsDst[-1])); */
                }
            }

            pBrush->u.bitmap.w = (uint16_t)psoTarget->sizlBitmap.cx;
            pBrush->u.bitmap.h = (uint16_t)psoTarget->sizlBitmap.cy;

            pBrush->fPattern = FALSE;

            bRc = TRUE;
        }
    }
#endif /* 0 */

    return bRc;
}
