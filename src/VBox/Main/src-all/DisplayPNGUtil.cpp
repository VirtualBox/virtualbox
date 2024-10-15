/* $Id: DisplayPNGUtil.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PNG utilities
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_DISPLAY
#include "DisplayImpl.h"

#include <iprt/alloc.h>

#include <png.h>

#define kMaxSizePNG 1024

typedef struct PNGWriteCtx
{
    uint8_t *pu8PNG;
    uint32_t cbPNG;
    uint32_t cbAllocated;
    int vrc;
} PNGWriteCtx;

static void PNGAPI png_write_data_fn(png_structp png_ptr, png_bytep p, png_size_t cb) RT_NOTHROW_DEF
{
    PNGWriteCtx *pCtx = (PNGWriteCtx *)png_get_io_ptr(png_ptr);
    LogFlowFunc(("png_ptr %p, p %p, cb %d, pCtx %p\n", png_ptr, p, cb, pCtx));

    if (pCtx && RT_SUCCESS(pCtx->vrc))
    {
        if (pCtx->cbAllocated - pCtx->cbPNG < cb)
        {
            uint32_t cbNew = pCtx->cbPNG + (uint32_t)cb;
            AssertReturnVoidStmt(cbNew > pCtx->cbPNG && cbNew <= _1G, pCtx->vrc = VERR_TOO_MUCH_DATA);
            cbNew = RT_ALIGN_32(cbNew, 4096) + 4096;

            void *pNew = RTMemRealloc(pCtx->pu8PNG, cbNew);
            if (!pNew)
            {
                pCtx->vrc = VERR_NO_MEMORY;
                return;
            }

            pCtx->pu8PNG = (uint8_t *)pNew;
            pCtx->cbAllocated = cbNew;
        }

        memcpy(pCtx->pu8PNG + pCtx->cbPNG, p, cb);
        pCtx->cbPNG += (uint32_t)cb;
    }
}

static void PNGAPI png_output_flush_fn(png_structp png_ptr) RT_NOTHROW_DEF
{
    NOREF(png_ptr);
    /* Do nothing. */
}

int DisplayMakePNG(uint8_t *pu8Data, uint32_t cx, uint32_t cy,
                   uint8_t **ppu8PNG, uint32_t *pcbPNG, uint32_t *pcxPNG, uint32_t *pcyPNG,
                   uint8_t fLimitSize)
{
    int vrc = VINF_SUCCESS;

    uint8_t * volatile pu8Bitmap = NULL; /* gcc setjmp  warning */
    uint32_t volatile cbBitmap = 0; /* gcc setjmp warning */
    uint32_t volatile cxBitmap = 0; /* gcc setjmp warning */
    uint32_t volatile cyBitmap = 0; /* gcc setjmp warning */

    if (!fLimitSize || (cx < kMaxSizePNG && cy < kMaxSizePNG))
    {
        /* Save unscaled screenshot. */
        pu8Bitmap = pu8Data;
        cbBitmap = cx * 4 * cy;
        cxBitmap = cx;
        cyBitmap = cy;
    }
    else
    {
        /* Large screenshot, scale. */
        if (cx > cy)
        {
            cxBitmap = kMaxSizePNG;
            cyBitmap = (kMaxSizePNG * cy) / cx;
        }
        else
        {
            cyBitmap = kMaxSizePNG;
            cxBitmap = (kMaxSizePNG * cx) / cy;
        }

        cbBitmap = cxBitmap * 4 * cyBitmap;

        pu8Bitmap = (uint8_t *)RTMemAlloc(cbBitmap);

        if (pu8Bitmap)
        {
            BitmapScale32(pu8Bitmap /*dst*/,
                          (int)cxBitmap, (int)cyBitmap,
                          pu8Data /*src*/,
                          (int)cx * 4,
                          (int)cx, (int)cy);
        }
        else
        {
            vrc = VERR_NO_MEMORY;
        }
    }

    LogFlowFunc(("%dx%d -> %dx%d\n", cx, cy, cxBitmap, cyBitmap));

    if (RT_SUCCESS(vrc))
    {
        png_bytep *row_pointers = (png_bytep *)RTMemAlloc(cyBitmap * sizeof(png_bytep));
        if (row_pointers)
        {
            png_infop info_ptr = NULL;
            png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                                          (png_voidp)NULL, /* error/warning context pointer */
                                                          (png_error_ptr)NULL, /* error function */
                                                          (png_error_ptr)NULL /* warning function */);
            if (png_ptr)
            {
                info_ptr = png_create_info_struct(png_ptr);
                if (info_ptr)
                {
#if RT_MSC_PREREQ(RT_MSC_VER_VC140)
#pragma warning(push,3)
                    if (!setjmp(png_jmpbuf(png_ptr)))
#pragma warning(pop)
#else
                    if (!setjmp(png_jmpbuf(png_ptr)))
#endif
                    {
                        PNGWriteCtx ctx;
                        ctx.pu8PNG = NULL;
                        ctx.cbPNG = 0;
                        ctx.cbAllocated = 0;
                        ctx.vrc = VINF_SUCCESS;

                        png_set_write_fn(png_ptr,
                                         (png_voidp)&ctx,
                                         png_write_data_fn,
                                         png_output_flush_fn);

                        png_set_IHDR(png_ptr, info_ptr,
                                     cxBitmap, cyBitmap,
                                     8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

                        png_bytep row_pointer = (png_bytep)pu8Bitmap;
                        unsigned i = 0;
                        for (; i < cyBitmap; i++, row_pointer += cxBitmap * 4)
                        {
                            row_pointers[i] = row_pointer;
                        }
                        png_set_rows(png_ptr, info_ptr, &row_pointers[0]);

                        png_write_info(png_ptr, info_ptr);
                        png_set_filler(png_ptr, 0, PNG_FILLER_AFTER);
                        png_set_bgr(png_ptr);

                        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_IDAT))
                            png_write_image(png_ptr, png_get_rows(png_ptr, info_ptr));

                        png_write_end(png_ptr, info_ptr);

                        vrc = ctx.vrc;

                        if (RT_SUCCESS(vrc))
                        {
                            *ppu8PNG = ctx.pu8PNG;
                            *pcbPNG = ctx.cbPNG;
                            *pcxPNG = cxBitmap;
                            *pcyPNG = cyBitmap;
                            LogFlowFunc(("PNG %d bytes, bitmap %d bytes\n", ctx.cbPNG, cbBitmap));
                        }
                    }
                    else
                    {
                        vrc = VERR_GENERAL_FAILURE; /* Something within libpng. */
                    }
                }
                else
                {
                    vrc = VERR_NO_MEMORY;
                }

                png_destroy_write_struct(&png_ptr, info_ptr ? &info_ptr
                                                            : (png_infopp)NULL);
            }
            else
            {
                vrc = VERR_NO_MEMORY;
            }

            RTMemFree(row_pointers);
        }
        else
        {
            vrc = VERR_NO_MEMORY;
        }
    }

    if (pu8Bitmap && pu8Bitmap != pu8Data)
    {
        RTMemFree(pu8Bitmap);
    }

    return vrc;

}
