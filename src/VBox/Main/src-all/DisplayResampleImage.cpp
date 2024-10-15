/* $Id: DisplayResampleImage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Image resampling code, used for snapshot thumbnails.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#include <iprt/types.h>

DECLINLINE(void) imageSetPixel (uint8_t *im, int x, int y, int color, int w)
{
    *(int32_t *)(im + y * w * 4 + x * 4) = color;
}

#define trueColorGetAlpha(c) (((c) & 0x7F000000) >> 24)
#define trueColorGetRed(c)   (((c) & 0xFF0000)   >> 16)
#define trueColorGetGreen(c) (((c) & 0x00FF00)   >> 8)
#define trueColorGetBlue(c)   ((c) & 0x0000FF)

/* Fast integer implementation for 32 bpp bitmap scaling.
 * Using fixed point values * 16.
 */
typedef int32_t FIXEDPOINT;
#define INT_TO_FIXEDPOINT(i) (FIXEDPOINT)((i) << 4)
#define FIXEDPOINT_TO_INT(v) (int)((v) >> 4)
#define FIXEDPOINT_FLOOR(v) ((v) & ~0xF)
#define FIXEDPOINT_FRACTION(v) ((v) & 0xF)

/* For 32 bit source only. */
void BitmapScale32 (uint8_t *dst,
                    int dstW, int dstH,
                    const uint8_t *src,
                    int iDeltaLine,
                    int srcW, int srcH)
{
    int x, y;

    for (y = 0; y < dstH; y++)
    {
        FIXEDPOINT sy1 = INT_TO_FIXEDPOINT(y * srcH) / dstH;
        FIXEDPOINT sy2 = INT_TO_FIXEDPOINT((y + 1) * srcH) / dstH;

        for (x = 0; x < dstW; x++)
        {
            FIXEDPOINT red = 0, green = 0, blue = 0;

            FIXEDPOINT sx1 = INT_TO_FIXEDPOINT(x * srcW) / dstW;
            FIXEDPOINT sx2 = INT_TO_FIXEDPOINT((x + 1) * srcW) / dstW;

            FIXEDPOINT spixels = (sx2 - sx1) * (sy2 - sy1);

            FIXEDPOINT sy = sy1;

            do
            {
                FIXEDPOINT yportion;
                if (FIXEDPOINT_FLOOR (sy) == FIXEDPOINT_FLOOR (sy1))
                {
                    yportion = INT_TO_FIXEDPOINT(1) - FIXEDPOINT_FRACTION(sy);
                    if (yportion > sy2 - sy1)
                    {
                        yportion = sy2 - sy1;
                    }
                    sy = FIXEDPOINT_FLOOR (sy);
                }
                else if (sy == FIXEDPOINT_FLOOR (sy2))
                {
                    yportion = FIXEDPOINT_FRACTION(sy2);
                }
                else
                {
                    yportion = INT_TO_FIXEDPOINT(1);
                }

                const uint8_t *pu8SrcLine = src + iDeltaLine * FIXEDPOINT_TO_INT(sy);
                FIXEDPOINT sx = sx1;
                do
                {
                    FIXEDPOINT xportion;
                    FIXEDPOINT pcontribution;
                    if (FIXEDPOINT_FLOOR (sx) == FIXEDPOINT_FLOOR (sx1))
                    {
                        xportion = INT_TO_FIXEDPOINT(1) - FIXEDPOINT_FRACTION(sx);
                        if (xportion > sx2 - sx1)
                        {
                            xportion = sx2 - sx1;
                        }
                        pcontribution = xportion * yportion;
                        sx = FIXEDPOINT_FLOOR (sx);
                    }
                    else if (sx == FIXEDPOINT_FLOOR (sx2))
                    {
                        xportion = FIXEDPOINT_FRACTION(sx2);
                        pcontribution = xportion * yportion;
                    }
                    else
                    {
                        xportion = INT_TO_FIXEDPOINT(1);
                        pcontribution = xportion * yportion;
                    }
                    /* Color depth specific code begin */
                    int32_t p = *(int32_t *)(pu8SrcLine + FIXEDPOINT_TO_INT(sx) * 4);
                    /* Color depth specific code end */
                    red   += trueColorGetRed (p) * pcontribution;
                    green += trueColorGetGreen (p) * pcontribution;
                    blue  += trueColorGetBlue (p) * pcontribution;

                    sx += INT_TO_FIXEDPOINT(1);
                } while (sx < sx2);

                sy += INT_TO_FIXEDPOINT(1);
            } while (sy < sy2);

            if (spixels != 0)
            {
                red /= spixels;
                green /= spixels;
                blue /= spixels;
            }
            /* Clamping to allow for rounding errors above */
            if (red > 255)
            {
                red = 255;
            }
            if (green > 255)
            {
                green = 255;
            }
            if (blue > 255)
            {
                blue = 255;
            }
            imageSetPixel (dst,
                           x, y,
                           ( ((int) red) << 16) + (((int) green) << 8) + ((int) blue),
                           dstW);
        }
    }
}
