/* $Id: shadowbuffer.cpp 114912 2026-08-10 12:03:20Z vitali.pelenjow@oracle.com $ */
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

#include "shadowbuffer.h"
#include "utils.h"
#include "orders.h"
#include "vrdpserv.h"
#include "bmpcache.h"

#include "videostream.h"
#include "bmpscale.h"

#include <iprt/heap.h>
#include <iprt/system.h>

/*
 * The shadow buffer can perform linear transformations when copying data from the
 * framebuffer to the shadow buffer pixel buffers.
 * Transformation is defined by the new position of the framebuffer bitmap origin
 * and increments required to reach next pixel in a line and a row:
 * pu8TransSrc, iTransDeltaPixel, iDeltaLine
 *
 * The following code access all pixels:
 *
 * uint8_t pu8SrcRow = pu8TransSrc;
 *
 * for (y = 0; y < iSrcHeight; y++)
 * {
 *     uint8_t pu8SrcLine = pu8SrcRow;
 *
 *     for (x = 0; x < iSrcWidth; x++)
 *     {
 *         uint8_t pu8Pixel = pu8SrcLine;
 *
 *         pu8SrcLine += iTransDeltaPixel;
 *     }
 *
 *     pu8SrcRow += iTransDeltaLine;
 * }
 *
 * An address of a pixel is:
 *
 * pu8Pixel = pu8TransSrc + y * iTransDeltaLine + x * pu8TransDeltaPixel;
 *
 * Given a original framebuffer rectangle defined with pu8Src, cbPixel, cbLine, x, y, w, h
 * with pu8Src being top left and W, H dimensions of the framebuffer following transformations
 * are supported: 0, 90, 180 and 270 degree rotations. w' = w - 1, h' = h - 1
 *
 * Trans pu8TransSrc                        iTransDeltaLine iTransDeltaPixel   xTrans       yTrans        wTrans hTrans
 * --------------------------------------------------------------------------------------------------------------------
 * 0     pu8Src                             cbLine          cbPixel            x            y             w      h
 * 90    pu8Src + cbPixel * w               -cbPixel        cbLine             y            W - (x + w')  h      w
 * 180   pu8Src + cbPixel * w + cbLine * h  -cbLine         -cbPixel           W - (x + w') H - (y + h')  w      h
 * 270   pu8Src + cbLine * h                cbPixel         -cbLine            H - (y + h') x             h      w
 *
 * All transformations use separate helper functions for performance reasons.
 */

typedef struct _PIXELBUFFER
{
    RGNRECT rect;

    uint32_t bitsPerPixel;
    uint32_t bytesPerPixel;

    uint32_t lineSize;
    uint8_t *pu8Pixels;

    /* The region that tracks updated area of the pixel buffer */
    REGION *prgnDirty;

    PFNGETPIXEL   pfnGetPixel;
    PFNSTOREPIXEL pfnStorePixel;
} PIXELBUFFER;

/* Transformed parameters of the original framebuffer VRDPBITSRECT */
typedef struct _VRDPTRANSBITSRECT
{
    const uint8_t *pu8Src;
    int            iDeltaLine;
    int            iDeltaPixel;
    RGNRECT        rect;
    unsigned       cBytesPerPixel;
    unsigned       cBitsPerPixel;
    PFNGETPIXEL    pfnGetPixel;
    PFNSTOREPIXEL  pfnStorePixel;
} VRDPTRANSBITSRECT;

typedef struct _VRDPTRANSFORM VRDPTRANSFORM;

struct _VRDPTRANSFORM
{
    unsigned uTransform;

    unsigned cFBWidth;  /* The framebuffer width. */
    unsigned cFBHeight; /* The framebuffer height. */

    unsigned cSBWidth;  /* The shadowbuffer width. */
    unsigned cSBHeight; /* The shadowbuffer height. */

    DECLCALLBACKMEMBER(void, pfnTransformWidthHeight,(unsigned *pw, unsigned *ph));

    DECLCALLBACKMEMBER(void, pfnTransformRect,(RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget));
    DECLCALLBACKMEMBER(void, pfnTransformRectToFB,(RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget));

    DECLCALLBACKMEMBER(void, pfnTransformPoint,(int *px, int *py, const unsigned wTarget, const unsigned hTarget));
    DECLCALLBACKMEMBER(void, pfnTransformPointToFB,(int *px, int *py, const unsigned wTarget, const unsigned hTarget));

    DECLCALLBACKMEMBER(void, pfnTransformDataBits,(VRDPTRANSBITSRECT *pTransRect, const VRDEDATABITS *pDataBits, const uint8_t *pu8Src, unsigned cbLine, const VRDPTRANSFORM *pTransform));

    /*
     * Copy rectangle from bits buffer to pixel buffer.
     *
     * No comparison is made with existing bits for MSB pixel buffers.
     * But for the main shadow buffer those bits are compared.
     *
     * @param ppb     Destination pixel buffer.
     * @param prect   Destination rectangle.
     * @param x       Left coordinate of the source bits.
     * @param y       Top coordinate of the source bits.
     * @param pu8Src  Points to top-left bits corner.
     * @param cbLine  Size in bytes of one scan line of bits.
     * @param cbPixel Size in bytes of one pixel.
     */
    DECLCALLBACKMEMBER(void, pfnCopyBitsToPixelBufferRect,(PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect));

    /*
     * Compare the source bits with pixel buffer bits.
     * Adjust width of the rectangle to exclude non-changed bits.
     *
     * @param ppb     Destination pixel buffer.
     * @param prect   Destination rectangle.
     * @param x       Left coordinate of the source bits.
     * @param y       Top coordinate of the source bits.
     * @param pu8Src  Points to top-left bits corner.
     * @param cbLine  Size in bytes of one scan line of bits.
     */
    DECLCALLBACKMEMBER(void, pfnAdjustRect,(PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect));
};


/* MICROSHADOWBUFFER::u8AllocationSource */
#define VRDP_MSB_ALLOC_PREALLOC   0
#define VRDP_MSB_ALLOC_HEAPMSB    1
#define VRDP_MSB_ALLOC_HEAPSYSTEM 2

typedef struct _MICROSHADOWBUFFER
{
    VRDPSHADOWBUFFER *psb;

    struct _MICROSHADOWBUFFER *upperMSB;
    struct _MICROSHADOWBUFFER *lowerMSB;

    REGION *prgnDirty;

    bool fUnderlayingDirtyAreaReported;
    bool fInList;
    bool fOutput;
    uint8_t u8AllocationSource;
    uint8_t u8Deleted;

    PIXELBUFFER pb;

    void *pvOrder;
    uint32_t cbOrder;

    RGNRECT rectAffected;

    int32_t i32Op;

} MICROSHADOWBUFFER;

/* Shadow buffer uses a list of preallocated memory blocks.
 * MSBs which fit in a block, that is most of orders, will use the list instead
 * of heap allocation. Only VRDE_ORDER_TEXT may not fit to a 256 bytes MSBBlock,
 * because it has variable number of glyphs bitmaps.
 * The size of one block is calculated to make the MSBBlock structure
 * size a power of 2.
 */
#define VRDP_MSB_BLOCK_SIZE (((sizeof (MICROSHADOWBUFFER) + 255) & (~255)) - sizeof (struct _MSBBlock *))

/* The number of preallocated blocks. */
#define VRDP_MSB_N_BLOCKS   (512)

typedef struct _MSBBlock
{
    /* Link to next free block in the free list. */
    struct _MSBBlock *next;

    /* MSB data */
    uint8_t au8[VRDP_MSB_BLOCK_SIZE];
} MSBBlock;

/* Verification of the structure sizes. */
#define VRDP_ASSERT_MSB_BLOCK_SIZES do {                                                       \
    SBLOG(("sizeof (MICROSHADOWBUFFER) = %d\n", sizeof (MICROSHADOWBUFFER)));                  \
    /* The structure size better be a power of 2. */                                           \
    AssertMsg(sizeof (MSBBlock) == 256, ("Size %d\n", sizeof (MSBBlock)));                     \
    /* Bytes available for data should be at least size of polyline order. */                  \
    AssertMsg (VRDP_MSB_BLOCK_SIZE - sizeof (MICROSHADOWBUFFER) >= sizeof (VRDEORDERPOLYLINE), \
               ("%d\n", VRDP_MSB_BLOCK_SIZE - sizeof (MICROSHADOWBUFFER)));                    \
} while (0)

struct _VRDPSHADOWBUFFER
{
    PIXELBUFFER pixelBuffer;

    MICROSHADOWBUFFER *topMSB;
    MICROSHADOWBUFFER *bottomMSB;

    uint8_t *src;
    uint32_t srcLineSize;

    bool fAdjustRects;
    bool fResized;

    /* Region includes all upper MSB rectangles which overlap a pixel buffer. */
    REGION *prgnOver;

    REGIONCTX *rgnCtx;

    VRDPTRANSFORM transform;

#ifdef DEBUG
    /* Takes account of the shadow buffer locking. */
    int cAccessed;
#endif /* DEBUG */

    bool fMSBDisabled;
    int cMSBs;
    int cMSBLimit;
    REGION *prgnRedraw;
    bool fResetBounds;
};

#ifdef VRDP_STAT_MSB
static struct MSBStat
{
    unsigned cFromHeap;
    unsigned cFromList;
    unsigned cFromHeapFree;
    unsigned cFromListFree;
    unsigned cAllocated;
    unsigned cMaxAllocated;
} msbStat;

#define MSB_STAT_INIT do {                                                \
    msbStat.cFromHeap = 0;                                                \
    msbStat.cFromList = 0;                                                \
    msbStat.cFromHeapFree = 0;                                            \
    msbStat.cFromListFree = 0;                                            \
    msbStat.cAllocated = 0;                                               \
    msbStat.cMaxAllocated = 0;                                            \
} while (0)

#define MSB_STAT_FROM_HEAP do { msbStat.cFromHeap++; } while (0)
#define MSB_STAT_FROM_LIST do { msbStat.cFromList++; } while (0)
#define MSB_STAT_FROM_HEAP_FREE do { msbStat.cFromHeapFree++; } while (0)
#define MSB_STAT_FROM_LIST_FREE do { msbStat.cFromListFree++; } while (0)
#define MSB_STAT_ALLOCATED do {                                           \
    msbStat.cAllocated++;                                                 \
    if (msbStat.cAllocated > msbStat.cMaxAllocated)                       \
    {                                                                     \
        msbStat.cMaxAllocated = msbStat.cAllocated;                       \
    }                                                                     \
} while (0)
#define MSB_STAT_DEALLOCATED do { msbStat.cAllocated--; } while (0)
#define MSB_STAT_LOG do {                                                 \
    VRDPLOGREL(("MSBSTAT: heap %d (%d), list %d (%d), max %d\n",          \
           msbStat.cFromHeap, msbStat.cFromHeapFree,                      \
           msbStat.cFromList, msbStat.cFromListFree,                      \
           msbStat.cMaxAllocated));                                       \
} while (0)
#else
#define MSB_STAT_INIT             do { } while (0)
#define MSB_STAT_FROM_HEAP        do { } while (0)
#define MSB_STAT_FROM_LIST        do { } while (0)
#define MSB_STAT_FROM_HEAP_FREE   do { } while (0)
#define MSB_STAT_FROM_LIST_FREE   do { } while (0)
#define MSB_STAT_ALLOCATED        do { } while (0)
#define MSB_STAT_DEALLOCATED      do { } while (0)
#define MSB_STAT_LOG              do { } while (0)
#endif /* VRDP_STAT_MSB */


#define SB_HANDLE_MAKE(gen, index) RT_MAKE_U64(index, gen)
#define SB_HANDLE_INDEX(h)         RT_LODWORD(h)
#define SB_HANDLE_GEN(h)           RT_HIDWORD(h)

typedef struct SBCOVERAREA
{
    SBHANDLE handle;
    RGNRECT rect;
} SBCOVERAREA;

/*
 * Global resources.
 */
typedef struct _VRDPSBSCREEN
{
    /* The screen index of the shadow buffer. [0:...]. */
    unsigned uScreenId;

    /* Whether the screen is currently enabled by VM. */
    bool fActive;

    /* Whether the screen can be accessed. */
    bool fAccessible;

    /* The position of the screen in VM. */
    int xOrigin;
    int yOrigin;

    /* The shadow buffer generation. Changes on resizing
     * and prevents access to resized shadow buffer.
     */
    volatile uint32_t u32AccessKey;

    /* Head of preallocated MSB free list. */
    MSBBlock *pFreeMSBBlocks;

    VDCONTEXT *pVideoDetector;
    SBCOVERAREA aCovers[32];

    /* The shadow buffer structure. */
    VRDPSHADOWBUFFER sb;

    /* Preallocated memory for MSBs. */
    MSBBlock aMSBBlocks[VRDP_MSB_N_BLOCKS];

    /* Heap for MSB structures (except for non drawing control MSBs) and pixel buffers. */
    uint8_t *pu8HeapMSB;    /* Allocated heap memory block. */
    uint32_t cbHeapMSB;     /* Size of the heap. */
    RTHEAPSIMPLE hHeapMSB;  /* Heap handle. */
} VRDPSBSCREEN;

static DECLCALLBACK(bool) vscVideoSourceStreamStart(void *pvCallback, uint32_t u32SourceStreamId, const RGNRECT *prect, int64_t timeStart);
static DECLCALLBACK(void) vscVideoSourceStreamStop(void *pvCallback, uint32_t u32SourceStreamId, const RGNRECT *prect);
static DECLCALLBACK(bool) vscIsValidRect(void *pvCallback, const RGNRECT *prect);

static VIDEOSTREAMCALLBACKS vsCallbacks =
{
    vscVideoSourceStreamStart,
    vscVideoSourceStreamStop,
    vscIsValidRect,
};

#define VRDP_SB_TO_SCREEN(__psb) ((VRDPSBSCREEN *)((uint8_t *)(__psb) - RT_UOFFSETOF(VRDPSBSCREEN, sb)))

typedef struct _VRDPSBCONTEXT
{
    /* Global shadow buffer critical section. */
    VRDPLock *pSBContextLock;

    /* The server which created the buffer. */
    VRDPServer *pServer;

    VHCONTEXT *pVideoHandler;

    /* The screen index [0:...] for which an output action to be retrieved. */
    unsigned uOutputScreenId;

    /* Number of created shadow buffers. */
    unsigned cScreens;

    /* Information about created shadow buffers. */
    VRDPSBSCREEN *paScreens;

    /* VRDPSBSCREEN structures follow. */
} VRDPSBCONTEXT;

/* Allocated in 'shadowBufferInit'. */
static VRDPSBCONTEXT *g_pCtx = NULL;


static bool sbIsScreenAccessible(const VRDPSBSCREEN *pScreen)
{
    return   pScreen != NULL
          && (pScreen->fActive && pScreen->fAccessible);
}

/* Lock shadow buffer if:
 * - the subsystem is initialized;
 * - uScreenId is active or is ~0;
 */
static bool sbLock (unsigned uScreenId)
{
    if (g_pCtx != NULL)
    {
        int rc = VRDPLock::Lock(g_pCtx->pSBContextLock);
        if (RT_SUCCESS(rc))
        {
            if (   uScreenId == ~0U
                || (   uScreenId < g_pCtx->cScreens
                    && sbIsScreenAccessible(&g_pCtx->paScreens[uScreenId])
                   )
               )
            {
                return true;
            }

            VRDPLock::Unlock(g_pCtx->pSBContextLock);
        }
    }

    return false;
}

static void sbUnlock (void)
{
    if (g_pCtx)
    {
        VRDPLock::Unlock(g_pCtx->pSBContextLock);
    }
}

#ifdef RT_STRICT
static bool sbLocked (void)
{
    return VRDPLock::IsOwner(g_pCtx->pSBContextLock);
}
#endif /* DEBUG */


/*
 * Functions which work differently for rotated screens.
 */

/*
 * A width and a height.
 */
static DECLCALLBACK(void) sbTransformWidthHeight0 (unsigned *pw, unsigned *ph)
{
    NOREF(pw);
    NOREF(ph);
}

static DECLCALLBACK(void) sbTransformWidthHeight90 (unsigned *pw, unsigned *ph)
{
    unsigned w = *ph;
    unsigned h = *pw;

    *pw = w;
    *ph = h;
}

static DECLCALLBACK(void) sbTransformWidthHeight180 (unsigned *pw, unsigned *ph)
{
    NOREF(pw);
    NOREF(ph);
}

static DECLCALLBACK(void) sbTransformWidthHeight270 (unsigned *pw, unsigned *ph)
{
    unsigned w = *ph;
    unsigned h = *pw;

    *pw = w;
    *ph = h;
}

/*
 * Rectangle from FB to SB.
 */
static DECLCALLBACK(void) sbTransformRect0 (RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget)
{
    NOREF(pRect);
    NOREF(wTarget);
    NOREF(hTarget);
}

static DECLCALLBACK(void) sbTransformRect90 (RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget)
{
    RGNRECT result;

    NOREF(wTarget);

    result.x = pRect->y;
    result.y = hTarget - (pRect->x + pRect->w);
    result.w = pRect->h;
    result.h = pRect->w;

    *pRect = result;
}

static DECLCALLBACK(void) sbTransformRect180 (RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget)
{
    RGNRECT result;

    result.x = wTarget - (pRect->x + pRect->w);
    result.y = hTarget - (pRect->y + pRect->h);
    result.w = pRect->w;
    result.h = pRect->h;

    *pRect = result;
}

static DECLCALLBACK(void) sbTransformRect270 (RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget)
{
    RGNRECT result;

    NOREF(hTarget);

    result.x = wTarget - (pRect->y + pRect->h);
    result.y = pRect->x;
    result.w = pRect->h;
    result.h = pRect->w;

    *pRect = result;
}

/*
 * Rectangle from SB to FB.
 */
static DECLCALLBACK(void) sbTransformRectToFB0 (RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget)
{
    NOREF(pRect);
    NOREF(wTarget);
    NOREF(hTarget);
}

static DECLCALLBACK(void) sbTransformRectToFB90 (RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget)
{
    RGNRECT result;

    NOREF(hTarget);

    result.x = wTarget - (pRect->y + pRect->h);
    result.y = pRect->x;
    result.w = pRect->h;
    result.h = pRect->w;

    *pRect = result;
}

static DECLCALLBACK(void) sbTransformRectToFB180 (RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget)
{
    RGNRECT result;

    result.x = wTarget - (pRect->x + pRect->w);
    result.y = hTarget - (pRect->y + pRect->h);
    result.w = pRect->w;
    result.h = pRect->h;

    *pRect = result;
}

static DECLCALLBACK(void) sbTransformRectToFB270 (RGNRECT *pRect, const unsigned wTarget, const unsigned hTarget)
{
    RGNRECT result;

    NOREF(wTarget);

    result.x = pRect->y;
    result.y = hTarget - (pRect->x + pRect->w);
    result.w = pRect->h;
    result.h = pRect->w;

    *pRect = result;
}

/*
 * Point from FB to SB.
 */
static DECLCALLBACK(void) sbTransformPoint0 (int *px, int *py, const unsigned wTarget, const unsigned hTarget)
{
    NOREF(px);
    NOREF(py);
    NOREF(wTarget);
    NOREF(hTarget);
}

static DECLCALLBACK(void) sbTransformPoint90 (int *px, int *py, const unsigned wTarget, const unsigned hTarget)
{
    int x, y;

    NOREF(wTarget);

    x = *py;
    y = hTarget - 1 - *px;

    *px = x;
    *py = y;
}

static DECLCALLBACK(void) sbTransformPoint180 (int *px, int *py, const unsigned wTarget, const unsigned hTarget)
{
    int x, y;

    x = wTarget - 1 - *px;
    y = hTarget - 1 - *py;

    *px = x;
    *py = y;
}

static DECLCALLBACK(void) sbTransformPoint270 (int *px, int *py, const unsigned wTarget, const unsigned hTarget)
{
    int x, y;

    NOREF(hTarget);

    x = wTarget - 1 - *py;
    y = *px;

    *px = x;
    *py = y;
}

/*
 * Point from SB to FB.
 */
static DECLCALLBACK(void) sbTransformPointToFB0 (int *px, int *py, const unsigned wTarget, const unsigned hTarget)
{
    NOREF(px);
    NOREF(py);
    NOREF(wTarget);
    NOREF(hTarget);
}

static DECLCALLBACK(void) sbTransformPointToFB90 (int *px, int *py, const unsigned wTarget, const unsigned hTarget)
{
    int x, y;

    NOREF(hTarget);

    x = wTarget - 1 - *py;
    y = *px;

    *px = x;
    *py = y;
}

static DECLCALLBACK(void) sbTransformPointToFB180 (int *px, int *py, const unsigned wTarget, const unsigned hTarget)
{
    int x, y;

    x = wTarget - 1 - *px;
    y = hTarget - 1 - *py;

    *px = x;
    *py = y;
}

static DECLCALLBACK(void) sbTransformPointToFB270 (int *px, int *py, const unsigned wTarget, const unsigned hTarget)
{
    int x, y;

    NOREF(wTarget);

    x = *py;
    y = hTarget - 1 - *px;

    *px = x;
    *py = y;
}

/*
 * Description of a bitmap in FB coordinates to the descrition in the SB coordinates.
 */
static DECLCALLBACK(void) sbTransformDataBits0 (VRDPTRANSBITSRECT *pTransRect, const VRDEDATABITS *pDataBits,
                                                const uint8_t *pu8Src, unsigned cbLine, const VRDPTRANSFORM *pTransform)
{
    RT_NOREF1(pTransform);
    pTransRect->pu8Src         = pu8Src;
    pTransRect->iDeltaLine     = cbLine;
    pTransRect->iDeltaPixel    = pDataBits->cbPixel;
    pTransRect->rect.x         = pDataBits->x;
    pTransRect->rect.y         = pDataBits->y;
    pTransRect->rect.w         = pDataBits->cWidth;
    pTransRect->rect.h         = pDataBits->cHeight;
    pTransRect->cBytesPerPixel = pDataBits->cbPixel;
    pTransRect->cBitsPerPixel  = pDataBits->cbPixel * 8;
    pTransRect->pfnGetPixel    = GetPixelFn (pTransRect->cBytesPerPixel);
    pTransRect->pfnStorePixel  = StorePixelFn (pTransRect->cBytesPerPixel);
}

static DECLCALLBACK(void) sbTransformDataBits90 (VRDPTRANSBITSRECT *pTransRect, const VRDEDATABITS *pDataBits,
                                                 const uint8_t *pu8Src, unsigned cbLine, const VRDPTRANSFORM *pTransform)
{
    pTransRect->pu8Src         = pu8Src +
                                 pDataBits->cbPixel * (pDataBits->cWidth - 1);
    pTransRect->iDeltaLine     = -pDataBits->cbPixel;
    pTransRect->iDeltaPixel    = cbLine;
    pTransRect->rect.x         = pDataBits->y;
    pTransRect->rect.y         = pTransform->cSBHeight - (pDataBits->x + pDataBits->cWidth);
    pTransRect->rect.w         = pDataBits->cHeight;
    pTransRect->rect.h         = pDataBits->cWidth;
    pTransRect->cBytesPerPixel = pDataBits->cbPixel;
    pTransRect->cBitsPerPixel  = pDataBits->cbPixel * 8;
    pTransRect->pfnGetPixel    = GetPixelFn (pTransRect->cBytesPerPixel);
    pTransRect->pfnStorePixel  = StorePixelFn (pTransRect->cBytesPerPixel);
}

static DECLCALLBACK(void) sbTransformDataBits180 (VRDPTRANSBITSRECT *pTransRect, const VRDEDATABITS *pDataBits,
                                                  const uint8_t *pu8Src, unsigned cbLine, const VRDPTRANSFORM *pTransform)
{
    pTransRect->pu8Src         = pu8Src +
                                 cbLine * (pDataBits->cHeight - 1) +
                                 pDataBits->cbPixel * (pDataBits->cWidth - 1);
    pTransRect->iDeltaLine     = -(int)cbLine;
    pTransRect->iDeltaPixel    = -pDataBits->cbPixel;
    pTransRect->rect.x         = pTransform->cSBWidth - (pDataBits->x + pDataBits->cWidth);
    pTransRect->rect.y         = pTransform->cSBHeight - (pDataBits->y + pDataBits->cHeight);
    pTransRect->rect.w         = pDataBits->cWidth;
    pTransRect->rect.h         = pDataBits->cHeight;
    pTransRect->cBytesPerPixel = pDataBits->cbPixel;
    pTransRect->cBitsPerPixel  = pDataBits->cbPixel * 8;
    pTransRect->pfnGetPixel    = GetPixelFn (pTransRect->cBytesPerPixel);
    pTransRect->pfnStorePixel  = StorePixelFn (pTransRect->cBytesPerPixel);
}

static DECLCALLBACK(void) sbTransformDataBits270 (VRDPTRANSBITSRECT *pTransRect, const VRDEDATABITS *pDataBits,
                                                  const uint8_t *pu8Src, unsigned cbLine, const VRDPTRANSFORM *pTransform)
{
    pTransRect->pu8Src         = pu8Src +
                                 cbLine * (pDataBits->cHeight - 1);
    pTransRect->iDeltaLine     = pDataBits->cbPixel;
    pTransRect->iDeltaPixel    = -(int)cbLine;
    pTransRect->rect.x         = pTransform->cSBWidth - (pDataBits->y + pDataBits->cHeight);
    pTransRect->rect.y         = pDataBits->x;
    pTransRect->rect.w         = pDataBits->cHeight;
    pTransRect->rect.h         = pDataBits->cWidth;
    pTransRect->cBytesPerPixel = pDataBits->cbPixel;
    pTransRect->cBitsPerPixel  = pDataBits->cbPixel * 8;
    pTransRect->pfnGetPixel    = GetPixelFn (pTransRect->cBytesPerPixel);
    pTransRect->pfnStorePixel  = StorePixelFn (pTransRect->cBytesPerPixel);
}

/*
 * Copy a rectangle from a bitmap described by the pTransRect to SB.
 */
static DECLCALLBACK(void) sbCopyBitsToPixelBufferRect0 (PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect)
{
    SBLOG(("prect %d,%d %dx%d from %d,%d\n", prect->x, prect->y, prect->w, prect->h, pTransRect->rect.x, pTransRect->rect.y));

    Assert (   (prect->y >= pTransRect->rect.y)
            && (prect->x >= pTransRect->rect.x)
            && (prect->y >= ppb->rect.y)
            && (prect->x >= ppb->rect.x));
    Assert (ppb->pu8Pixels);

    /* Pointer to begin of rectangle to be copied from in the bits buffer. */
    const uint8_t *s = pTransRect->pu8Src +
                       (prect->y - pTransRect->rect.y) * pTransRect->iDeltaLine +
                       (prect->x - pTransRect->rect.x) * pTransRect->iDeltaPixel;

    /* Pointer to begin of rectangle to be copied to the pixel buffer. */
    uint8_t *d = ppb->pu8Pixels +
                 (prect->y - ppb->rect.y) * ppb->lineSize +
                 (prect->x - ppb->rect.x) * ppb->bytesPerPixel;

    uint32_t h = prect->h;

    if (pTransRect->cBytesPerPixel == ppb->bytesPerPixel)
    {
        int cb = prect->w * pTransRect->cBytesPerPixel;

        while (h--)
        {
            /* Copy the scanline to destination pixel buffer. */
            memcpy (d, s, cb);

            s += pTransRect->iDeltaLine;
            d += ppb->lineSize;
        }
    }
    else
    {
        while (h--)
        {
            /* Convert the scanline to destination pixel buffer. */
            ConvertColors (s, pTransRect->cBitsPerPixel, prect->w, d, ppb->bitsPerPixel, prect->w * ppb->bytesPerPixel);

            s += pTransRect->iDeltaLine;
            d += ppb->lineSize;
        }
    }
}

static DECLCALLBACK(void) sbCopyBitsToPixelBufferRect90 (PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect)
{
    SBLOG(("prect %d,%d %dx%d from %d,%d\n", prect->x, prect->y, prect->w, prect->h, pTransRect->rect.x, pTransRect->rect.y));

    Assert (   (prect->y >= pTransRect->rect.y)
            && (prect->x >= pTransRect->rect.x)
            && (prect->y >= ppb->rect.y)
            && (prect->x >= ppb->rect.x));
    Assert (ppb->pu8Pixels);

    /* Pointer to begin of rectangle to be copied from in the bits buffer. */
    const uint8_t *s = pTransRect->pu8Src +
                       (prect->y - pTransRect->rect.y) * pTransRect->iDeltaLine +
                       (prect->x - pTransRect->rect.x) * pTransRect->iDeltaPixel;

    /* Pointer to begin of rectangle to be copied to the pixel buffer. */
    uint8_t *d = ppb->pu8Pixels +
                 (prect->y - ppb->rect.y) * ppb->lineSize +
                 (prect->x - ppb->rect.x) * ppb->bytesPerPixel;

    int x, y;

    if (pTransRect->cBytesPerPixel == ppb->bytesPerPixel)
    {
        const uint8_t *pu8SrcRow = s;
        uint8_t *pu8DstRow = d;

        for (y = 0; y < (int)prect->h; y++)
        {
            const uint8_t *pu8SrcLine = pu8SrcRow;
            uint8_t *pu8DstLine = pu8DstRow;

            for (x = 0; x < (int)prect->w; x++)
            {
                unsigned pixel = pTransRect->pfnGetPixel (pu8SrcLine, 0);
                pu8DstLine = ppb->pfnStorePixel (pu8DstLine, pixel);

                pu8SrcLine += pTransRect->iDeltaPixel;
            }

            pu8SrcRow += pTransRect->iDeltaLine;
            pu8DstRow += ppb->lineSize;
        }
    }
    else
    {
        const uint8_t *pu8SrcRow = s;
        uint8_t *pu8DstRow = d;

        for (y = 0; y < (int)prect->h; y++)
        {
            const uint8_t *pu8SrcLine = pu8SrcRow;
            uint8_t *pu8DstLine = pu8DstRow;

            for (x = 0; x < (int)prect->w; x++)
            {
                ConvertColors (pu8SrcLine, pTransRect->cBitsPerPixel, 1, pu8DstLine, ppb->bitsPerPixel, ppb->bytesPerPixel);

                pu8DstLine += ppb->bytesPerPixel;
                pu8SrcLine += pTransRect->iDeltaPixel;
            }

            pu8SrcRow += pTransRect->iDeltaLine;
            pu8DstRow += ppb->lineSize;
        }
    }
}

static DECLCALLBACK(void) sbCopyBitsToPixelBufferRect180 (PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect)
{
    SBLOG(("prect %d,%d %dx%d from %d,%d\n", prect->x, prect->y, prect->w, prect->h, pTransRect->rect.x, pTransRect->rect.y));

    Assert (   (prect->y >= pTransRect->rect.y)
            && (prect->x >= pTransRect->rect.x)
            && (prect->y >= ppb->rect.y)
            && (prect->x >= ppb->rect.x));
    Assert (ppb->pu8Pixels);

    /* Pointer to begin of rectangle to be copied from in the bits buffer. */
    const uint8_t *s = pTransRect->pu8Src +
                       (prect->y - pTransRect->rect.y) * pTransRect->iDeltaLine +
                       (prect->x - pTransRect->rect.x) * pTransRect->iDeltaPixel;

    /* Pointer to begin of rectangle to be copied to the pixel buffer. */
    uint8_t *d = ppb->pu8Pixels +
                 (prect->y - ppb->rect.y) * ppb->lineSize +
                 (prect->x - ppb->rect.x) * ppb->bytesPerPixel;

    int x, y;

    if (pTransRect->cBytesPerPixel == ppb->bytesPerPixel)
    {
        const uint8_t *pu8SrcRow = s;
        uint8_t *pu8DstRow = d;

        for (y = 0; y < (int)prect->h; y++)
        {
            const uint8_t *pu8SrcLine = pu8SrcRow;
            uint8_t *pu8DstLine = pu8DstRow;

            for (x = 0; x < (int)prect->w; x++)
            {
                unsigned pixel = pTransRect->pfnGetPixel (pu8SrcLine, 0);
                pu8DstLine = ppb->pfnStorePixel (pu8DstLine, pixel);

                pu8SrcLine += pTransRect->iDeltaPixel;
            }

            pu8SrcRow += pTransRect->iDeltaLine;
            pu8DstRow += ppb->lineSize;
        }
    }
    else
    {
        const uint8_t *pu8SrcRow = s;
        uint8_t *pu8DstRow = d;

        for (y = 0; y < (int)prect->h; y++)
        {
            const uint8_t *pu8SrcLine = pu8SrcRow;
            uint8_t *pu8DstLine = pu8DstRow;

            for (x = 0; x < (int)prect->w; x++)
            {
                ConvertColors (pu8SrcLine, pTransRect->cBitsPerPixel, 1, pu8DstLine, ppb->bitsPerPixel, ppb->bytesPerPixel);

                pu8DstLine += ppb->bytesPerPixel;
                pu8SrcLine += pTransRect->iDeltaPixel;
            }

            pu8SrcRow += pTransRect->iDeltaLine;
            pu8DstRow += ppb->lineSize;
        }
    }
}

static DECLCALLBACK(void) sbCopyBitsToPixelBufferRect270 (PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect)
{
    SBLOG(("prect %d,%d %dx%d from %d,%d\n", prect->x, prect->y, prect->w, prect->h, pTransRect->rect.x, pTransRect->rect.y));

    Assert (   (prect->y >= pTransRect->rect.y)
            && (prect->x >= pTransRect->rect.x)
            && (prect->y >= ppb->rect.y)
            && (prect->x >= ppb->rect.x));
    Assert (ppb->pu8Pixels);

    /* Pointer to begin of rectangle to be copied from in the bits buffer. */
    const uint8_t *s = pTransRect->pu8Src +
                       (prect->y - pTransRect->rect.y) * pTransRect->iDeltaLine +
                       (prect->x - pTransRect->rect.x) * pTransRect->iDeltaPixel;

    /* Pointer to begin of rectangle to be copied to the pixel buffer. */
    uint8_t *d = ppb->pu8Pixels +
                 (prect->y - ppb->rect.y) * ppb->lineSize +
                 (prect->x - ppb->rect.x) * ppb->bytesPerPixel;

    int x, y;

    if (pTransRect->cBytesPerPixel == ppb->bytesPerPixel)
    {
        const uint8_t *pu8SrcRow = s;
        uint8_t *pu8DstRow = d;

        for (y = 0; y < (int)prect->h; y++)
        {
            const uint8_t *pu8SrcLine = pu8SrcRow;
            uint8_t *pu8DstLine = pu8DstRow;

            for (x = 0; x < (int)prect->w; x++)
            {
                unsigned pixel = pTransRect->pfnGetPixel (pu8SrcLine, 0);
                pu8DstLine = ppb->pfnStorePixel (pu8DstLine, pixel);

                pu8SrcLine += pTransRect->iDeltaPixel;
            }

            pu8SrcRow += pTransRect->iDeltaLine;
            pu8DstRow += ppb->lineSize;
        }
    }
    else
    {
        const uint8_t *pu8SrcRow = s;
        uint8_t *pu8DstRow = d;

        for (y = 0; y < (int)prect->h; y++)
        {
            const uint8_t *pu8SrcLine = pu8SrcRow;
            uint8_t *pu8DstLine = pu8DstRow;

            for (x = 0; x < (int)prect->w; x++)
            {
                ConvertColors (pu8SrcLine, pTransRect->cBitsPerPixel, 1, pu8DstLine, ppb->bitsPerPixel, ppb->bytesPerPixel);

                pu8DstLine += ppb->bytesPerPixel;
                pu8SrcLine += pTransRect->iDeltaPixel;
            }

            pu8SrcRow += pTransRect->iDeltaLine;
            pu8DstRow += ppb->lineSize;
        }
    }
}

static DECLCALLBACK(void) sbAdjustRect0 (PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect)
{
    Assert (   (prect->y >= pTransRect->rect.y)
            && (prect->x >= pTransRect->rect.x)
            && (prect->y >= ppb->rect.y)
            && (prect->x >= ppb->rect.x));
    Assert (ppb->pu8Pixels);
    Assert (prect->w > 0);

    if (ppb->bitsPerPixel != pTransRect->cBitsPerPixel)
    {
        SBLOG(("Skipping AdjustRect. bitsPerPixel don't match: ppb %d transrect %d!!!\n", ppb->bitsPerPixel, pTransRect->cBitsPerPixel));
        return;
    }

    /* Initial values for new rectangle X coordinates. */
    int32_t xLeftmost = prect->w; /* +inf */
    int32_t xRightmost = -1;      /* -inf */

    /* Pointer to begin of the source rectangle. */
    const uint8_t *s = pTransRect->pu8Src +
                       (prect->y - pTransRect->rect.y) * pTransRect->iDeltaLine +
                       (prect->x - pTransRect->rect.x) * pTransRect->iDeltaPixel;

    /* Pointer to begin of the destination rectangle. */
    uint8_t *d = ppb->pu8Pixels +
                 (prect->y - ppb->rect.y) * ppb->lineSize +
                 (prect->x - ppb->rect.x) * ppb->bytesPerPixel;

    uint32_t h = prect->h;

    while (h--)
    {
        /* Find leftmost different pixel in the current scanline. */
        int32_t x = 0;

        while (x < xLeftmost)
        {
            uint32_t dstPixel = ppb->pfnGetPixel (d, x);
            uint32_t srcPixel = pTransRect->pfnGetPixel (s, x);

            if (dstPixel != srcPixel)
            {
                xLeftmost = x;
                break;
            }

            x++;
        }

        /* Find the rightmost pixel. Only pixels [xFence, prect->w) should be processed. */
        int32_t xFence = RT_MAX (xLeftmost, xRightmost);

        x = prect->w - 1;

        while (x >= xFence)
        {
            uint32_t dstPixel = ppb->pfnGetPixel (d, x);
            uint32_t srcPixel = pTransRect->pfnGetPixel (s, x);

            if (dstPixel != srcPixel)
            {
                xRightmost = x;
                break;
            }

            x--;
        }

        s += pTransRect->iDeltaLine;
        d += ppb->lineSize;
    }

    SBLOG(("RECT ADJUSTED: %d,%d -> %d,%d\n", prect->x, prect->x + prect->w, prect->x + xLeftmost, prect->x + xRightmost + 1));

    if (xLeftmost > xRightmost)
    {
        /* Empty update. */
        SBLOG(("RECT %d,%d %dx%d -> empty\n", prect->x, prect->y, prect->w, prect->h));
        rgnUpdateRectWidth (prect, prect->x, 0);
    }
    else
    {
#ifdef DEBUG_sunlover
        if (xLeftmost != 0 || xRightmost - xLeftmost + 1 != (int)prect->w)
        {
            SBLOG(("RECT %d,%d %dx%d -> adjusted %d-%d\n", prect->x, prect->y, prect->w, prect->h, xLeftmost, xRightmost));
        }
#endif /* DEBUG_sunlover */
        rgnUpdateRectWidth (prect, prect->x + xLeftmost, xRightmost - xLeftmost + 1);
    }

    return;
}

static DECLCALLBACK(void) sbAdjustRect90 (PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect)
{
    /** @todo implement */
    NOREF(ppb);
    NOREF(prect);
    NOREF(pTransRect);
}

static DECLCALLBACK(void) sbAdjustRect180 (PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect)
{
    /** @todo implement */
    NOREF(ppb);
    NOREF(prect);
    NOREF(pTransRect);
}

static DECLCALLBACK(void) sbAdjustRect270 (PIXELBUFFER *ppb, RGNRECT *prect, const VRDPTRANSBITSRECT *pTransRect)
{
    /** @todo implement */
    NOREF(ppb);
    NOREF(prect);
    NOREF(pTransRect);
}


static void sbInitNormalTransBitsRect (VRDPTRANSBITSRECT *pTransRect, PIXELBUFFER *ppb)
{
    pTransRect->pu8Src         = ppb->pu8Pixels;
    pTransRect->iDeltaLine     = ppb->lineSize;
    pTransRect->iDeltaPixel    = ppb->bytesPerPixel;
    pTransRect->rect           = ppb->rect;
    pTransRect->cBytesPerPixel = ppb->bytesPerPixel;
    pTransRect->cBitsPerPixel  = ppb->bitsPerPixel;
    pTransRect->pfnGetPixel    = GetPixelFn (pTransRect->cBytesPerPixel);
    pTransRect->pfnStorePixel  = StorePixelFn (pTransRect->cBytesPerPixel);
}

static void sbInitTransform (VRDPSHADOWBUFFER *psb, unsigned uTransform, const VRDPBITSRECT *pBitsRect)
{
    psb->transform.cFBWidth = pBitsRect->rect.w;
    psb->transform.cFBHeight = pBitsRect->rect.h;

    switch (uTransform)
    {
        default:
        case VRDP_TRANSFORM_ROTATE_0:
        {
            psb->transform.uTransform = VRDP_TRANSFORM_ROTATE_0;

            psb->transform.cSBWidth  = psb->transform.cFBWidth;
            psb->transform.cSBHeight = psb->transform.cFBHeight;

            psb->transform.pfnTransformWidthHeight      = sbTransformWidthHeight0;
            psb->transform.pfnTransformRect             = sbTransformRect0;
            psb->transform.pfnTransformRectToFB         = sbTransformRectToFB0;
            psb->transform.pfnTransformPoint            = sbTransformPoint0;
            psb->transform.pfnTransformPointToFB        = sbTransformPointToFB0;
            psb->transform.pfnTransformDataBits         = sbTransformDataBits0;
            psb->transform.pfnCopyBitsToPixelBufferRect = sbCopyBitsToPixelBufferRect0;
            psb->transform.pfnAdjustRect                = sbAdjustRect0;
        } break;

        case VRDP_TRANSFORM_ROTATE_90:
        {
            psb->transform.uTransform = VRDP_TRANSFORM_ROTATE_90;

            psb->transform.cSBWidth  = psb->transform.cFBHeight;
            psb->transform.cSBHeight = psb->transform.cFBWidth;

            psb->transform.pfnTransformWidthHeight      = sbTransformWidthHeight90;
            psb->transform.pfnTransformRect             = sbTransformRect90;
            psb->transform.pfnTransformRectToFB         = sbTransformRectToFB90;
            psb->transform.pfnTransformPoint            = sbTransformPoint90;
            psb->transform.pfnTransformPointToFB        = sbTransformPointToFB90;
            psb->transform.pfnTransformDataBits         = sbTransformDataBits90;
            psb->transform.pfnCopyBitsToPixelBufferRect = sbCopyBitsToPixelBufferRect90;
            psb->transform.pfnAdjustRect                = sbAdjustRect90;
        } break;

        case VRDP_TRANSFORM_ROTATE_180:
        {
            psb->transform.uTransform = VRDP_TRANSFORM_ROTATE_180;

            psb->transform.cSBWidth  = psb->transform.cFBWidth;
            psb->transform.cSBHeight = psb->transform.cFBHeight;

            psb->transform.pfnTransformWidthHeight      = sbTransformWidthHeight180;
            psb->transform.pfnTransformRect             = sbTransformRect180;
            psb->transform.pfnTransformRectToFB         = sbTransformRectToFB180;
            psb->transform.pfnTransformPoint            = sbTransformPoint180;
            psb->transform.pfnTransformPointToFB        = sbTransformPointToFB180;
            psb->transform.pfnTransformDataBits         = sbTransformDataBits180;
            psb->transform.pfnCopyBitsToPixelBufferRect = sbCopyBitsToPixelBufferRect180;
            psb->transform.pfnAdjustRect                = sbAdjustRect180;
        } break;

        case VRDP_TRANSFORM_ROTATE_270:
        {
            psb->transform.uTransform = VRDP_TRANSFORM_ROTATE_270;

            psb->transform.cSBWidth  = psb->transform.cFBHeight;
            psb->transform.cSBHeight = psb->transform.cFBWidth;

            psb->transform.pfnTransformWidthHeight      = sbTransformWidthHeight270;
            psb->transform.pfnTransformRect             = sbTransformRect270;
            psb->transform.pfnTransformRectToFB         = sbTransformRectToFB270;
            psb->transform.pfnTransformPoint            = sbTransformPoint270;
            psb->transform.pfnTransformPointToFB        = sbTransformPointToFB270;
            psb->transform.pfnTransformDataBits         = sbTransformDataBits270;
            psb->transform.pfnCopyBitsToPixelBufferRect = sbCopyBitsToPixelBufferRect270;
            psb->transform.pfnAdjustRect                = sbAdjustRect270;
        } break;
    }
}


/*
 * A separate heap for MSB data structures and pixel buffers.
 */
static void *msbHeapAlloc(VRDPSHADOWBUFFER *psb, uint32_t cbAlloc)
{
    return RTHeapSimpleAlloc(VRDP_SB_TO_SCREEN(psb)->hHeapMSB, cbAlloc, 0);
}

static void msbHeapFree(VRDPSHADOWBUFFER *psb, void *p)
{
    RTHeapSimpleFree(VRDP_SB_TO_SCREEN(psb)->hHeapMSB, p);
}


/*
 * MSB pixel buffer allocation/free.
 */
static int msbPixelsAlloc(MICROSHADOWBUFFER *pmsb, const RGNRECT *pRect)
{
    int rc = VINF_SUCCESS;

    if (pmsb->pb.pu8Pixels == NULL)
    {
        NOREF(pRect); /** @todo allocation optimization. */

        uint32_t cbAlloc = pmsb->pb.lineSize * pmsb->pb.rect.h;

        if (cbAlloc > 0)
        {
            pmsb->pb.pu8Pixels = (uint8_t *)msbHeapAlloc(pmsb->psb, cbAlloc);
            SBLOG(("Allocated pixels for msb %p, pu8Pixels: %p\n", pmsb, pmsb->pb.pu8Pixels));

            if (pmsb->pb.pu8Pixels == NULL)
            {
                rc = VERR_NO_MEMORY;
            }
        }
    }

    return rc;
}

static void msbPixelsFree(MICROSHADOWBUFFER *pmsb)
{
    if (pmsb->pb.pu8Pixels)
    {
        msbHeapFree(pmsb->psb, pmsb->pb.pu8Pixels);
        pmsb->pb.pu8Pixels = NULL;
    }
}


static int pbInit (REGIONCTX *rgnCtx, uint32_t u32AccessKey, PIXELBUFFER *ppb, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t bitsPerPixel)
{
    ppb->rect.x        = x;
    ppb->rect.y        = y;
    ppb->rect.h        = h;
    ppb->rect.w        = w;
    ppb->bitsPerPixel  = bitsPerPixel;

    ppb->bytesPerPixel = (ppb->bitsPerPixel + 7) / 8;

    ppb->lineSize      = ppb->rect.w * ppb->bytesPerPixel;
    ppb->lineSize      = (ppb->lineSize + 3) & ~3;

    /* Lazy allocation for MSBs. The pixel buffer memory is not allocated until
     * some pixels actually go to buffer.
     */
    ppb->pu8Pixels = NULL;

    ppb->prgnDirty = rgnCreateEmpty (rgnCtx, ppb->rect.x, ppb->rect.y, ppb->rect.w, ppb->rect.h, u32AccessKey);

    if (!ppb->prgnDirty)
    {
        return VERR_NO_MEMORY;
    }

    ppb->pfnGetPixel   = GetPixelFn (ppb->bytesPerPixel);
    ppb->pfnStorePixel = StorePixelFn (ppb->bytesPerPixel);

    return VINF_SUCCESS;
}

static void pbUninit (PIXELBUFFER *ppb)
{
    if (ppb)
    {
        if (ppb->prgnDirty)
        {
            rgnDelete (ppb->prgnDirty);
            ppb->prgnDirty = NULL;
        }

        Assert(ppb->pu8Pixels == NULL);

        memset(&ppb->rect, 0, sizeof(ppb->rect));

        ppb->bitsPerPixel = 0;
        ppb->bytesPerPixel = 0;

        ppb->lineSize = 0;

        ppb->pfnGetPixel = NULL;
        ppb->pfnStorePixel = NULL;
    }

    return;
}

static MICROSHADOWBUFFER *msbAlloc (VRDPSHADOWBUFFER *psb, void *pvOrder, uint32_t cbOrder, bool fControlMSB)
{
    /* MSB limit: if a MSBs can not be allocated, switch to redraw updates
     *            until number of MSBs goes back to 0.
     */
    uint8_t u8AllocationSource;

    MICROSHADOWBUFFER *pmsb = NULL;

    if (psb->fMSBDisabled)
    {
        SBLOG(("MSBLIMIT: d\n"));
        return NULL;
    }

    if (!fControlMSB && psb->cMSBs >= psb->cMSBLimit)
    {
        /* No need to have a long queue of drawing MSBs,
         * even if the queue has limited memory consumption.
         */
        SBLOG(("MSBLIMIT: n\n"));
        psb->fMSBDisabled = true;
        /* Looks like guest sends too many graphics updates.
         * Reduce the queue depth, do not overwhelm the client.
         * Now the condition will be reached even faster.
         */
        psb->cMSBLimit = psb->cMSBLimit / 2 + 8;
        SBLOG(("cMSBLimit dec -> %d\n", psb->cMSBLimit));

        return NULL;
    }

    uint32_t cbAlloc = sizeof(MICROSHADOWBUFFER) + cbOrder;

    MSBBlock *pBlock = VRDP_SB_TO_SCREEN(psb)->pFreeMSBBlocks;

    if (   cbAlloc <= VRDP_MSB_BLOCK_SIZE
        && pBlock)
    {
        /* Use a preallocated buffer. */
        VRDP_SB_TO_SCREEN(psb)->pFreeMSBBlocks = pBlock->next;

        pBlock->next = NULL;

        pmsb = (MICROSHADOWBUFFER *)&pBlock->au8[0];

        Assert (   (uint8_t *)&VRDP_SB_TO_SCREEN(psb)->aMSBBlocks[0] < (uint8_t *)pmsb
                && (uint8_t *)pmsb           < (uint8_t *)&VRDP_SB_TO_SCREEN(psb)->aMSBBlocks[RT_ELEMENTS(VRDP_SB_TO_SCREEN(psb)->aMSBBlocks)]);

        u8AllocationSource = VRDP_MSB_ALLOC_PREALLOC;

        MSB_STAT_FROM_LIST;
    }
    else
    {
        /* No place in preallocated buffers. */
        if (fControlMSB)
        {
            /* Allocate from runtime heap. */
            pmsb = (MICROSHADOWBUFFER *)VRDPMemAlloc(cbAlloc);
            u8AllocationSource = VRDP_MSB_ALLOC_HEAPSYSTEM;
        }
        else
        {
            /* Allocate from the separate MSB heap. */
            pmsb = (MICROSHADOWBUFFER *)msbHeapAlloc(psb, cbAlloc);
            u8AllocationSource = VRDP_MSB_ALLOC_HEAPMSB;
        }

        MSB_STAT_FROM_HEAP;
    }

    if (pmsb)
    {
        memset (pmsb, 0, sizeof (MICROSHADOWBUFFER));

        pmsb->psb     = psb;
        pmsb->cbOrder = cbOrder;
        pmsb->pvOrder = (uint8_t *)pmsb + sizeof (MICROSHADOWBUFFER);
        pmsb->u8AllocationSource = u8AllocationSource;

        if (cbOrder)
        {
            memcpy (pmsb->pvOrder, pvOrder, cbOrder);
        }

        MSB_STAT_ALLOCATED;
    }
    else
    {
        /* Failed to allocate a new MSB. Fallback. */
        SBLOG(("MSBLIMIT: l\n"));

        psb->fMSBDisabled = true;
    }

    return pmsb;
}

static void msbFree (MICROSHADOWBUFFER *pmsb)
{
    Assert (pmsb);

    VRDPSHADOWBUFFER *psb = pmsb->psb;
    Assert (psb);

    if (pmsb->u8AllocationSource == VRDP_MSB_ALLOC_PREALLOC)
    {
        /* The block was from preallocated list. */
        MSBBlock *pBlock = (MSBBlock *)((uint8_t *)pmsb - (sizeof (MSBBlock) - VRDP_MSB_BLOCK_SIZE));

        Assert(   (uint8_t *)&VRDP_SB_TO_SCREEN(psb)->aMSBBlocks[0] < (uint8_t *)pmsb
               && (uint8_t *)pmsb < (uint8_t *)&VRDP_SB_TO_SCREEN(psb)->aMSBBlocks[RT_ELEMENTS(VRDP_SB_TO_SCREEN(psb)->aMSBBlocks)]);
        Assert(((uint8_t *)pBlock - (uint8_t *)&VRDP_SB_TO_SCREEN(psb)->aMSBBlocks[0]) % sizeof (MSBBlock) == 0);
        Assert(pBlock->next == NULL);

        pBlock->next = VRDP_SB_TO_SCREEN(pmsb->psb)->pFreeMSBBlocks;
        VRDP_SB_TO_SCREEN(psb)->pFreeMSBBlocks = pBlock;
        MSB_STAT_FROM_LIST_FREE;
    }
    else if (pmsb->u8AllocationSource == VRDP_MSB_ALLOC_HEAPMSB)
    {
        /* MSB was allocated from MSB heap. */
        msbHeapFree(psb, pmsb);
        MSB_STAT_FROM_HEAP_FREE;
    }
    else
    {
        Assert(pmsb->u8AllocationSource == VRDP_MSB_ALLOC_HEAPSYSTEM);
        /* MSB was allocated from heap. */
        VRDPMemFree (pmsb);
        MSB_STAT_FROM_HEAP_FREE;
    }

    MSB_STAT_DEALLOCATED;
}

static void deleteMSB (MICROSHADOWBUFFER *pmsb)
{
    if (pmsb)
    {
        pmsb->u8Deleted++;

        /* Check if the MSB was used by output thread during resize and was not completely deleted. */
        bool fPostponedMSB = (pmsb->fInList == false);

        if (pmsb->fInList)
        {
            /* Remove the msb from the list. */
            if (pmsb->lowerMSB)
            {
                pmsb->lowerMSB->upperMSB = pmsb->upperMSB;
            }
            else
            {
                pmsb->psb->bottomMSB = pmsb->upperMSB;
            }

            if (pmsb->upperMSB)
            {
                pmsb->upperMSB->lowerMSB = pmsb->lowerMSB;
            }
            else
            {
                pmsb->psb->topMSB = pmsb->lowerMSB;
            }

            pmsb->fInList = false;
            pmsb->lowerMSB = NULL;
            pmsb->upperMSB = NULL;

            pmsb->psb->cMSBs--;
            Assert(pmsb->psb->cMSBs >= 0);
        }

        if (!pmsb->fOutput)
        {
            /* The MSB is not being used by output thread and can be deleted. */
            if (!fPostponedMSB)
            {
                /* Only have to delete pixels if the MSB was not postponed.
                 * @todo MSBs and the shadow buffer heap need reference counting.
                 */
                msbPixelsFree (pmsb);
            }

            pbUninit (&pmsb->pb);

            msbFree (pmsb);
        }
        else
        {
            /* The MSB is used by the output thread. Will be deleted by 'shadowBufferCancelAction'
             * that will call the 'deleteMSB' again with 'pmsb->fOutput' = false.
             */
            SBLOG(("postponed MSB %p\n", pmsb));
        }
    }
}


static bool createMSB (VRDPSHADOWBUFFER *psb, int32_t i32Op, const RGNRECT *pRect, void *pvOrder, uint32_t cbOrder, bool fControlMSB)
{
    /*
     * Allocate MSB structure and the pixel buffer.
     * Insert the new MSB as top MSB in the MSB list.
     */
    Assert (sbLocked ());

    SBLOG(("createMSB: i32Op = %d, pvOrder = %p, cbOrder = %d\n", i32Op, pvOrder, cbOrder));

    /* 'pRect' must be within the shadow buffer. If it is wrong -> fallback. */
    if (   pRect
        && !rgnIsRectWithin (&psb->pixelBuffer.rect, pRect))
    {
        static int scLogged = 0;
        if (scLogged < 32)
        {
            scLogged++;
            VRDPLOGREL(("Ignored MSB rectangle %d,%d %dx%d for %d, %p, %d\n",
                      pRect->x, pRect->y, pRect->w, pRect->h, i32Op, pvOrder, cbOrder));
        }
        psb->fMSBDisabled = true;
        return false;
    }

    /* Disable comparing data copied to shadow buffer with data that is already there.
     * Orders are not applied to shadow buffer and it is not exact copy of the clients
     * display image.
     */
    psb->fAdjustRects = false;

    static const RGNRECT sRectEmpty = {0, 0, 0, 0};
    if (pRect == NULL)
    {
        pRect = &sRectEmpty;
    }

    MICROSHADOWBUFFER *pmsb = msbAlloc (psb, pvOrder, cbOrder, fControlMSB);

    if (pmsb)
    {
        /* Initialize MSB. */
        int rc = pbInit (psb->rgnCtx, VRDP_SB_TO_SCREEN(psb)->u32AccessKey, &pmsb->pb, pRect->x, pRect->y, pRect->w, pRect->h, psb->pixelBuffer.bitsPerPixel);

        if (RT_SUCCESS(rc))
        {
            pmsb->i32Op = i32Op;
            pmsb->rectAffected = *pRect;

            /* Insert the new MSB to top. */
            if (psb->topMSB == NULL)
            {
                psb->topMSB = psb->bottomMSB = pmsb;
            }
            else
            {
                pmsb->lowerMSB = psb->topMSB;

                psb->topMSB->upperMSB = pmsb;
                psb->topMSB = pmsb;
            }

            pmsb->fInList = true;

            psb->cMSBs++;
        }

        if (RT_FAILURE(rc))
        {
            SBLOG(("Failed to create MSB %p!!!\n", pmsb));
            deleteMSB (pmsb);
            pmsb = NULL;
        }

        SBLOG(("createOrderMSB: created %p\n", pmsb));

#if 0 // def LOG_ENABLED
        pmsb = psb->topMSB;

        while (pmsb)
        {
            SBLOG(("MSB %p: up %p, down %p, sdr %p, cbOrder %d\n",
                     pmsb, pmsb->upperMSB, pmsb->lowerMSB,
                     0 /*pmsb->savedDirtyRegion*/, pmsb->cbOrder));

            pmsb = pmsb->lowerMSB;
        }
#endif /* LOG_ENABLED */
        SBLOG(("MSBLIMIT: output %d,%d %dx%d %d\n", pRect->x, pRect->y, pRect->w, pRect->h, i32Op));
    }
    else
    {
        SBLOG(("MSBLIMIT: redraw %d,%d %dx%d %d\n", pRect->x, pRect->y, pRect->w, pRect->h, i32Op));
        /* Do not generate the MSB. Make a redraw update instead. */
        if (pRect != &sRectEmpty)
        {
            rgnAddRect (psb->prgnRedraw, pRect);
        }
    }

    return (pmsb != NULL);
}

static void sbMSBFallback (VRDPSBSCREEN *pScreen, MICROSHADOWBUFFER *pmsb)
{
    /* Do not touch existing MSBs, just add the current MSB rectangle to the redraw. */
    rgnAddRect (pScreen->sb.prgnRedraw, &pmsb->rectAffected);
    pScreen->sb.fMSBDisabled = true;
}


/* Helper function to delete shadow buffer. Called from 'shadowBufferResize'.
 */
static void sbDelete (VRDPSHADOWBUFFER *psb)
{
    Assert(g_pCtx);
    Assert(psb);

    TESTLOG(("Enter\n"));

    if (sbLock (~0U))
    {
        rgnDelete (psb->prgnRedraw);
        psb->prgnRedraw = NULL;
        rgnDelete (psb->prgnOver);
        psb->prgnOver = NULL;

        VRDPMemFree(psb->pixelBuffer.pu8Pixels);
        psb->pixelBuffer.pu8Pixels = NULL;

        pbUninit (&psb->pixelBuffer);

        MICROSHADOWBUFFER *pmsb = psb->topMSB;

        while (pmsb)
        {
            TESTLOG(("pmsb %p, pmsb->lowerMSB %p, pmsb->upperMSB %p\n", pmsb, pmsb->lowerMSB, pmsb->upperMSB));

            MICROSHADOWBUFFER *pnext = pmsb->lowerMSB;

            deleteMSB (pmsb);

            pmsb = pnext;
        }
        psb->topMSB = NULL;
        psb->bottomMSB = NULL;

        MSB_STAT_LOG;

        /* Free any resources in the REGION layer. */
        if (psb->rgnCtx)
        {
            rgnCtxRelease(psb->rgnCtx);
            psb->rgnCtx = NULL;
        }

        psb->src = NULL;
        psb->srcLineSize = 0;

        psb->fAdjustRects = 0;
        psb->fResized = 0;

        memset (&psb->transform, 0, sizeof (psb->transform));

        psb->fMSBDisabled = false;
        psb->cMSBs = 0;
        psb->cMSBLimit = 0;
        psb->fResetBounds = false;

        sbUnlock ();
    }
}

static void sbChangeAccessKey (VRDPSBSCREEN *pScreen)
{
    pScreen->u32AccessKey++;

    if (pScreen->u32AccessKey == VRDP_SB_NULL_ACCESS_KEY)
    {
        pScreen->u32AccessKey++;
    }
}

static VRDPSBSCREEN *sbResolveScreenId (unsigned uScreenId)
{
    if (g_pCtx != NULL && uScreenId < g_pCtx->cScreens)
    {
        VRDPSBSCREEN *pScreen = &g_pCtx->paScreens[uScreenId];

        return pScreen;
    }

#ifdef DEBUG_sunlover
    AssertFailed();
#endif
    return NULL;
}


/*
 * Public functions.
 */

/* Called once when the server is created.
 *
 * Initialize the global resources and create the requested number
 * of shadow buffers.
 *
 * @param cScreens How many buffers to create.
 *
 * @return VBox error code.
 */
int shadowBufferInit (VRDPServer *pServer, unsigned cScreens)
{
    VRDP_ASSERT_MSB_BLOCK_SIZES;

    if (cScreens == 0)
    {
        return VERR_INVALID_PARAMETER;
    }

    Assert (g_pCtx == NULL);

    /* Allocate memory for the shadow buffer context. */
    g_pCtx = (VRDPSBCONTEXT *)VRDPMemAllocZ (sizeof (VRDPSBCONTEXT) + cScreens * sizeof (VRDPSBSCREEN));

    if (!g_pCtx)
    {
        return VERR_NO_MEMORY;
    }

    /* Initialize the context. */
    int rc = VRDPLock::Create("ShadowBuffer", &g_pCtx->pSBContextLock);
    AssertRC(rc);

    if (RT_SUCCESS (rc))
    {
        g_pCtx->pServer = pServer;
        g_pCtx->uOutputScreenId = 0;
        g_pCtx->cScreens = cScreens;

        g_pCtx->paScreens = (VRDPSBSCREEN *)&g_pCtx[1];

        unsigned i;
        for (i = 0; i < cScreens; i++)
        {
            VRDPSBSCREEN *pScreen = &g_pCtx->paScreens[i];

            pScreen->uScreenId    = i;
            pScreen->fActive      = false;
            pScreen->fAccessible  = true;
            pScreen->u32AccessKey = VRDP_SB_NULL_ACCESS_KEY;

            /* Initialize preallocated MSB list. */
            MSBBlock *pBlock = &pScreen->aMSBBlocks[0];

            unsigned j;
            for (j = 0; j < RT_ELEMENTS(pScreen->aMSBBlocks) - 1; j++)
            {
                pBlock->next = pBlock + 1;
                pBlock = pBlock->next;
            }

            Assert (pBlock == &pScreen->aMSBBlocks[RT_ELEMENTS(pScreen->aMSBBlocks) - 1]);
            pBlock->next = NULL;

            pScreen->pFreeMSBBlocks = &pScreen->aMSBBlocks[0];

            /* The heap is used for MSBs and for their pixel buffers.
             * Therefore the allocation size should depend on the framebuffer size.
             */
            uint32_t cbHeapMSB = 16*_1M; /// @todo get stats, optimize PB allocs and see how much is actually needed

            uint32_t const cbPage = RTSystemGetPageSize();
            pScreen->cbHeapMSB = RT_ALIGN_32(cbHeapMSB, cbPage);

            pScreen->pu8HeapMSB = (uint8_t *)VRDPMemAlloc(pScreen->cbHeapMSB);

            if (!pScreen->pu8HeapMSB)
            {
                rc = VERR_NO_MEMORY;
            }
            else
            {
                rc = RTHeapSimpleInit(&pScreen->hHeapMSB, pScreen->pu8HeapMSB, pScreen->cbHeapMSB);
            }

            if (RT_FAILURE(rc))
            {
                break;
            }

            /* The 'pScreen->sb' remains zero initialized. */
            rc = videoDetectorContextCreate(&pScreen->pVideoDetector, &vsCallbacks, pScreen, pServer);
            if (RT_FAILURE(rc))
            {
                break;
            }
        }
    }

    if (RT_FAILURE (rc))
    {
        shadowBufferUninit ();
    }

    return rc;
}

/* Called once when the server is shutted down or when init fails.
 *
 * Free all 'g_pCtx' resources.
 *
 * @param cScreens How many buffers to create.
 *
 * @return VBox error code.
 */
void shadowBufferUninit (void)
{
    if (g_pCtx == NULL)
    {
        return;
    }

    unsigned i;
    for (i = 0; i < g_pCtx->cScreens; i++)
    {
        VRDPSBSCREEN *pScreen = &g_pCtx->paScreens[i];

        videoDetectorContextDelete(pScreen->pVideoDetector);
        pScreen->pVideoDetector = NULL;

        if (pScreen->pu8HeapMSB)
        {
            VRDPMemFree(pScreen->pu8HeapMSB);
            pScreen->pu8HeapMSB = NULL;
        }
        pScreen->cbHeapMSB = 0;
        pScreen->hHeapMSB = NIL_RTHEAPSIMPLE;

        Assert (!pScreen->fActive);
    }

    VRDPLock::Delete(&g_pCtx->pSBContextLock);

#ifdef DEBUG
    memset (g_pCtx, 0xCC, sizeof (VRDPSBCONTEXT) + g_pCtx->cScreens * sizeof (VRDPSBSCREEN));
#endif /* DEBUG */

    VRDPMemFree (g_pCtx);
    g_pCtx = NULL;
}

bool shadowBufferVerifyScreenId (unsigned uScreenId)
{
    return g_pCtx != NULL && uScreenId < g_pCtx->cScreens;
}

unsigned shadowBufferQueryScreenCount (void)
{
    return g_pCtx? g_pCtx->cScreens: 0;
}

void shadowBufferMapMouse (unsigned uScreenId, int *px, int *py)
{
    if (sbLock (~0U))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId(uScreenId);

        if (pScreen != NULL)
        {
            *px += pScreen->xOrigin;
            *py += pScreen->yOrigin;
        }

        sbUnlock ();
    }
}

/* VM thread */
/* Resize the specified shadow buffer.
 *
 */
void shadowBufferResize (unsigned uScreenId, VRDPBITSRECT *pBitsRect, unsigned uTransform)
{
    SBLOG((":%d %dx%d, bitsPerPixel %d\n",
           uScreenId, pBitsRect? pBitsRect->rect.w: 0, pBitsRect? pBitsRect->rect.h: 0, pBitsRect? pBitsRect->cBitsPerPixel: 0));

    if (!g_pCtx || uScreenId >= g_pCtx->cScreens)
    {
        AssertFailed ();
        return;
    }

    if (sbLock (~0U))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId(uScreenId);

        if (pScreen == NULL)
        {
            sbUnlock ();
            return;
        }

        /* Prevent the access with old key. */
        sbChangeAccessKey (pScreen);

        /* Always reset video detector and covers. */
        memset (pScreen->aCovers, 0, sizeof (pScreen->aCovers));
        videoDetectorReset(pScreen->pVideoDetector, false);

        if (   pScreen->fActive
            && pBitsRect)
        {
            /*
             * Optimize the case when only the source bitmap pointer has been changed.
             */
            if (   pScreen->sb.transform.uTransform     == uTransform
                && pScreen->xOrigin                     == pBitsRect->rect.x
                && pScreen->yOrigin                     == pBitsRect->rect.y
                && pScreen->sb.pixelBuffer.rect.w       == pBitsRect->rect.w
                && pScreen->sb.pixelBuffer.rect.h       == pBitsRect->rect.h
                && pScreen->sb.pixelBuffer.bitsPerPixel == pBitsRect->cBitsPerPixel
                && pScreen->sb.srcLineSize              == pBitsRect->cbLine
               )
            {
                /* Update the source pointer, clear shadow buffer and set the dirty
                 * region to entire screen, which will redraw the client screen.
                 */
                SBLOG(("Same resolution. Skipping the resize.\n"));

                pScreen->sb.src = (uint8_t *)pBitsRect->pu8Bits;

                PIXELBUFFER *ppb = &pScreen->sb.pixelBuffer;

                if (ppb->prgnDirty)
                {
                    rgnDelete (ppb->prgnDirty);
                }
                ppb->prgnDirty = rgnCreateEmpty (pScreen->sb.rgnCtx,
                                                 ppb->rect.x, ppb->rect.y, ppb->rect.w, ppb->rect.h,
                                                 pScreen->u32AccessKey);
                rgnAddRect(ppb->prgnDirty, &ppb->rect);

                /* Copy the source content to the shadow buffer. */
                shadowBufferBitmapUpdate (uScreenId, 0, 0, ppb->rect.w, ppb->rect.h);

                sbUnlock ();
                return;
            }
        }

        if (pScreen->fActive)
        {
            sbDelete (&pScreen->sb);
            pScreen->fActive = false;
        }

        if (pBitsRect)
        {
            pScreen->xOrigin = pBitsRect->rect.x;
            pScreen->yOrigin = pBitsRect->rect.y;

            pScreen->sb.topMSB    = NULL;
            pScreen->sb.bottomMSB = NULL;

            pScreen->sb.src = (uint8_t *)pBitsRect->pu8Bits;
            pScreen->sb.srcLineSize = pBitsRect->cbLine;

            SBLOG(("src %p\n", pScreen->sb.src));

            /* Enable comparing bitmap updates with shadow buffer pixels. */
            pScreen->sb.fAdjustRects = true;
            pScreen->sb.fResized = true;
#ifdef DEBUG
            pScreen->sb.cAccessed = 0;
#endif /* DEBUG */
            pScreen->sb.fMSBDisabled = false;
            pScreen->sb.cMSBs = 0;
            pScreen->sb.cMSBLimit = VRDP_MSB_N_BLOCKS;
            pScreen->sb.fResetBounds = false;

            int rc;

            pScreen->sb.rgnCtx = rgnCtxCreate ();

            if (!pScreen->sb.rgnCtx)
            {
                rc = VERR_NO_MEMORY;
            }
            else
            {
                /* Transformation is initialized before the pixel buffer initialization. */
                sbInitTransform (&pScreen->sb, uTransform, pBitsRect);

                rc = pbInit (pScreen->sb.rgnCtx, pScreen->u32AccessKey, &pScreen->sb.pixelBuffer,
                             0, 0, pScreen->sb.transform.cSBWidth, pScreen->sb.transform.cSBHeight,
                             pBitsRect->cBitsPerPixel);

                if (RT_SUCCESS(rc))
                {
                    pScreen->sb.prgnRedraw = rgnCreateEmpty (pScreen->sb.rgnCtx, 0, 0, pScreen->sb.transform.cSBWidth, pScreen->sb.transform.cSBHeight, pScreen->u32AccessKey);

                    if (!pScreen->sb.prgnRedraw)
                    {
                        rc = VERR_NO_MEMORY;
                    }
                }

                if (RT_SUCCESS(rc))
                {
                    pScreen->sb.prgnOver = rgnCreateEmpty (pScreen->sb.rgnCtx, 0, 0, pScreen->sb.transform.cSBWidth, pScreen->sb.transform.cSBHeight, pScreen->u32AccessKey);

                    if (!pScreen->sb.prgnOver)
                    {
                        rc = VERR_NO_MEMORY;
                    }

                    /* Allocate pixels for shadow buffer. */
                    if (RT_SUCCESS(rc))
                    {
                        size_t cbPixels = pScreen->sb.pixelBuffer.lineSize * pScreen->sb.pixelBuffer.rect.h;

                        pScreen->sb.pixelBuffer.pu8Pixels = (uint8_t *)VRDPMemAllocZ (cbPixels);

                        if (!pScreen->sb.pixelBuffer.pu8Pixels)
                        {
                            rc = VERR_NO_MEMORY;
                        }
                    }
                }
            }

            MSB_STAT_INIT;

            if (RT_FAILURE(rc))
            {
                VRDPLOGRELLIMIT(16, ("VRDP: warning: no shadowbuffer %Rrc\n", rc));
                sbDelete (&pScreen->sb);
            }
            else
            {
                pScreen->fActive = true;
            }
        }

        sbUnlock ();
    }
}

void shadowBufferDestroyBuffers (void)
{
    if (!g_pCtx)
    {
        return;
    }

    if (sbLock (~0U))
    {
        unsigned uScreenId = 0;

        for (; uScreenId < g_pCtx->cScreens; uScreenId++)
        {
            VRDPSBSCREEN *pScreen = sbResolveScreenId(uScreenId);

            if (pScreen != NULL)
            {
                /* Prevent the access with old key. */
                sbChangeAccessKey (pScreen);

                if (pScreen->fActive)
                {
                    sbDelete (&pScreen->sb);
                    pScreen->fActive = false;
                }
            }
        }

        sbUnlock ();
    }
}


static void sbAdjustCoords (RGNRECT *prect, uint32_t cx, uint32_t cy)
{
    /* Correct negative x and y coordinates. */
    if (prect->x < 0)
    {
        prect->x += prect->w; /* Compute xRight which is also the new width. */

        prect->w = (prect->x < 0)? 0: prect->x;

        prect->x = 0;
    }

    if (prect->y < 0)
    {
        prect->y += prect->h; /* Compute xBottom, which is also the new height. */

        prect->h = (prect->y < 0)? 0: prect->y;

        prect->y = 0;
    }

    /* Also check if coords are greater than the display resolution. */
    if (prect->x + prect->w > cx)
    {
        prect->w = cx > (uint32_t)prect->x? cx - prect->x: 0;
    }

    if (prect->y + prect->h > cy)
    {
        prect->h = cy > (uint32_t)prect->y? cy - prect->y: 0;
    }
}

/* Copy the region from 'x,y pu8Src, cbLine' pixels to the pixel buffer. */
static void sbCopyBitsToPixelBufferRgn (VRDPSHADOWBUFFER *psb,
                                        PIXELBUFFER *ppb,
                                        REGION *prgn,
                                        const VRDPTRANSBITSRECT *pTransRect,
                                        bool fMayAdjustRects)
{
    SBLOG(("Enter\n"));

    RGNRECT *prect;

    bool fAdjustRects = (psb->fAdjustRects && fMayAdjustRects);

    Assert(ppb->pu8Pixels != NULL);

    rgnEnumRect (prgn);

    while ((prect = rgnNextRect (prgn)) != NULL)
    {
        if (fAdjustRects)
        {
            psb->transform.pfnAdjustRect (ppb, prect, pTransRect);

            if (rgnIsRectEmpty (prect))
            {
                /* The rectangle bits are not changed. */
                continue;
            }
        }

        psb->transform.pfnCopyBitsToPixelBufferRect (ppb, prect, pTransRect);
    }

    if (fAdjustRects)
    {
        rgnRemoveEmptyBricks (prgn);
    }

    SBLOG(("Leave\n"));
}

/* Copy bits to pixel buffer excluding overlapping region. */
static void sbUpdatePixelBuffer (VRDPSHADOWBUFFER *psb,
                                 PIXELBUFFER *ppb,
                                 const RGNRECT *prect,
                                 const VRDPTRANSBITSRECT *pTransRect)
{
    SBLOG(("Enter rect %d,%d %dx%d src %d,%d\n", prect->x, prect->y, prect->w, prect->h, pTransRect->rect.x, pTransRect->rect.y));

    /* Calculate update region which must be copied to the MSB pixel buffer.
     *
     * 1) Calculate subregion of the MSB overlapped region, bounded by the rectSect.
     * 2) Invert the subregion, obtaining the region to be updated.
     */

    REGION *prgnSect = rgnCreateEmpty (psb->rgnCtx, prect->x, prect->y, prect->w, prect->h, VRDP_SB_TO_SCREEN(psb)->u32AccessKey);
    Assert(prgnSect);

    rgnCut (prgnSect, psb->prgnOver, prect);

    rgnInvert (prgnSect);

#ifdef VRDP_DEBUG_SB
    REGION *prgnTest = rgnCreateEmpty (psb->rgnCtx, prect->x, prect->y, prect->w, prect->h, VRDP_SB_TO_SCREEN(psb)->u32AccessKey);
    rgnCut (prgnTest, psb->prgnOver, prect);
    rgnAdd (prgnTest, prgnSect);
    rgnInvert (prgnTest);
    if (!rgnIsEmpty (prgnTest))
    {
        SBLOG(("Overlapping failed!!!"));
    }
    rgnDelete (prgnTest);
#endif

    if (!rgnIsEmpty (prgnSect))
    {
        sbCopyBitsToPixelBufferRgn (psb, ppb, prgnSect,
                                    pTransRect, true);

        rgnAdd (ppb->prgnDirty, prgnSect);
    }

    rgnDelete (prgnSect);

    SBLOG(("Leave\n"));
}


/* Copies source bits described by 'pTransRect'.
 *
 * @param psb      The shadow buffer pointer.
 */
static void sbCopyBitsToPixelBuffers (VRDPSBSCREEN *pScreen,
                                      const VRDPTRANSBITSRECT *pTransRect)
{
    SBLOG(("Enter\n"));

    Assert(sbLocked ());

    VRDPSHADOWBUFFER *psb = &pScreen->sb;

#ifdef DEBUG
    /* Format of bits and shadow buffer should match. */
    if (pTransRect->cBitsPerPixel != psb->pixelBuffer.bitsPerPixel)
    {
        /* The framebuffer implementation does not support a direct VRAM access.
         * Performance will decrease.
         */
        SBLOG(("Warning: bits format is incorrect %d, should be be %d!!!\n",
               pTransRect->cBitsPerPixel, psb->pixelBuffer.bitsPerPixel));
    }
#endif /* DEBUG */

    /* Bits rectangle must fit in the shadow buffer. */
    if (  pTransRect->rect.x < 0 || (pTransRect->rect.x + pTransRect->rect.w > psb->pixelBuffer.rect.w)
       || pTransRect->rect.y < 0 || (pTransRect->rect.y + pTransRect->rect.h > psb->pixelBuffer.rect.h))
    {
        SBLOG(("Bits rectangle is incorrect %d,%d, %dx%d, sb %dx%d!!!\n",
               pTransRect->rect.x, pTransRect->rect.y, pTransRect->rect.w, pTransRect->rect.h, psb->pixelBuffer.rect.w, psb->pixelBuffer.rect.h));
        return;
    }

    const RGNRECT rectUpdate = pTransRect->rect;

    if (psb->fMSBDisabled)
    {
        rgnAddRect(pScreen->sb.prgnRedraw, &rectUpdate);
        return;
    }

    /* Process MSBs from top to bottom. Bitmap pixels must be copied to MSB's
     * which overlap the bitmap.
     */
    MICROSHADOWBUFFER *pmsb = psb->topMSB;

    Assert (rgnIsEmpty(psb->prgnOver));

    unsigned i;
    for (i = 0; i < RT_ELEMENTS(pScreen->aCovers); i++)
    {
        if (pScreen->aCovers[i].handle != SB_HANDLE_NULL)
        {
            rgnAddRect (psb->prgnOver, &pScreen->aCovers[i].rect);
        }
    }

    bool fFallback = false;

    while (pmsb)
    {
        SBLOG(("msb = %p, %d,%d %dx%d, op %d\n", pmsb, pmsb->pb.rect.x, pmsb->pb.rect.y, pmsb->pb.rect.w, pmsb->pb.rect.h, pmsb->i32Op));

        /* Find intersection between record to be updated and the MSB rectangle. */
        RGNRECT rectSect;

        if (rgnIntersectRects(&rectSect, &pmsb->pb.rect, &rectUpdate))
        {
            /* Allocate the MSB pixel buffer. */
            int rc = msbPixelsAlloc(pmsb, &rectSect);

            if (RT_FAILURE(rc))
            {
                /* Can't process this MSB properly. Fallback to bitmap updates. */
                sbMSBFallback(pScreen, pmsb);
                rgnAddRect(pScreen->sb.prgnRedraw, &rectUpdate);
                fFallback = true;
                break;
            }

            sbUpdatePixelBuffer (psb, &pmsb->pb, &rectSect, pTransRect);

            rgnAddRect (psb->prgnOver, &pmsb->pb.rect);
        }

        pmsb = pmsb->lowerMSB;
    }

    if (!fFallback)
    {
        sbUpdatePixelBuffer (psb, &psb->pixelBuffer, &rectUpdate, pTransRect);
    }

    rgnReset (psb->prgnOver, rgnGetUniq (psb->prgnOver));

    SBLOG(("Leave\n"));
}

static DECLCALLBACK(bool) vscVideoSourceStreamStart(void *pvCallback, uint32_t u32SourceStreamId, const RGNRECT *prect, int64_t timeStart)
{
    VRDPSBSCREEN *pScreen = (VRDPSBSCREEN *)pvCallback;
    AssertPtrReturn(pScreen, false);

    if (g_pCtx->pVideoHandler)
    {
        bool fAccepted = videoHandlerSourceStreamStart(g_pCtx->pVideoHandler, u32SourceStreamId, prect, timeStart, pScreen->uScreenId, false, NULL);
        if (fAccepted)
        {
            return true;
        }
    }

    return false;
}

static DECLCALLBACK(void) vscVideoSourceStreamStop(void *pvCallback, uint32_t u32SourceStreamId, const RGNRECT *prect)
{
    VRDPSBSCREEN *pScreen = (VRDPSBSCREEN *)pvCallback;
    AssertPtrReturnVoid(pScreen);

    if (g_pCtx->pVideoHandler)
    {
        videoHandlerSourceStreamStop(g_pCtx->pVideoHandler, u32SourceStreamId);
    }

#if 1
    /* Make sure the area is updated. */
    /// @todo this is not synchronous, use a scheduled redraw update.
    shadowBufferBitmapUpdate(pScreen->uScreenId, prect->x, prect->y, prect->w, prect->h);
#else
    /// @todo this probably causes redraw problems, rewrite using an output event.
    /* Find and remove corresponding covers. */
    unsigned i;
    for (i = 0; i < RT_ELEMENTS(pScreen->aCovers); i++)
    {
        if (pScreen->aCovers[i].handle != SB_HANDLE_NULL)
        {
            if (   pScreen->aCovers[i].rect.x == prect->x
                && pScreen->aCovers[i].rect.y == prect->y
                && pScreen->aCovers[i].rect.w == prect->w
                && pScreen->aCovers[i].rect.h == prect->h
               )
            {
                pScreen->aCovers[i].handle = SB_HANDLE_NULL;
            }
        }
    }

    /* Make sure the area is updated. Use a scheduled redraw update. */
    rgnAddRect (pScreen->sb.prgnRedraw, prect);
#endif

    return;
}

static DECLCALLBACK(bool) vscIsValidRect(void *pvCallback, const RGNRECT *prect)
{
    VRDPSBSCREEN *pScreen = (VRDPSBSCREEN *)pvCallback;
    AssertPtrReturn(pScreen, false);
    return rgnIsRectWithin(&pScreen->sb.pixelBuffer.rect, prect);
}

void shadowBufferUpdateComplete(void)
{
    if (!sbLock (~0U))
    {
        return;
    }

    unsigned uScreenId;

    for (uScreenId = 0; uScreenId < g_pCtx->cScreens; uScreenId++)
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId(uScreenId);

        if (sbIsScreenAccessible(pScreen))
        {
            videoDetectorUpdateComplete(pScreen->pVideoDetector);

            if (!pScreen->sb.fMSBDisabled)
            {
                /* Increase limit to 25%. */
                if (pScreen->sb.cMSBLimit < VRDP_MSB_N_BLOCKS)
                {
                    pScreen->sb.cMSBLimit = pScreen->sb.cMSBLimit + pScreen->sb.cMSBLimit / 4;
                    if (pScreen->sb.cMSBLimit > VRDP_MSB_N_BLOCKS)
                    {
                        pScreen->sb.cMSBLimit = VRDP_MSB_N_BLOCKS;
                    }
                    SBLOG(("cMSBLimit inc -> %d\n", pScreen->sb.cMSBLimit));
                }
            }

            /* Check if it is possible to return to MSBs. This is done here after all current
             * updates have been processed.
             */
#if 1
            /** @todo there should be 2 redraw regions: 1) MSB fallback, which is processed when
             * pScreen->sb.cMSBs == 0 and 2) scheduled redraw updates, which is processed in
             * normal mode.
             */
            if (pScreen->sb.cMSBs == 0)
#else
            if (!pScreen->sb.fMSBDisabled || pScreen->sb.cMSBs == 0)
#endif
            {
                /* Process scheduled redraw updates. If in normal mode or if
                 * MSB were disabled and now all of them has been processed.
                 */
                if (!rgnIsEmpty(pScreen->sb.prgnRedraw))
                {
                    VRDPTRANSBITSRECT transRect;

                    transRect.pu8Src         = pScreen->sb.src;
                    transRect.iDeltaLine     = pScreen->sb.srcLineSize;
                    transRect.iDeltaPixel    = pScreen->sb.pixelBuffer.bytesPerPixel;
                    transRect.rect           = pScreen->sb.pixelBuffer.rect;
                    transRect.cBytesPerPixel = pScreen->sb.pixelBuffer.bytesPerPixel;
                    transRect.cBitsPerPixel  = pScreen->sb.pixelBuffer.bitsPerPixel;
                    transRect.pfnGetPixel    = GetPixelFn (transRect.cBytesPerPixel);
                    transRect.pfnStorePixel  = StorePixelFn (transRect.cBytesPerPixel);

                    sbCopyBitsToPixelBufferRgn (&pScreen->sb, &pScreen->sb.pixelBuffer, pScreen->sb.prgnRedraw,
                                                &transRect,
                                                false);
                    SBLOG(("MSBLIMIT: copied redraw\n"));

                    rgnAdd (pScreen->sb.pixelBuffer.prgnDirty, pScreen->sb.prgnRedraw);

                    rgnReset (pScreen->sb.prgnRedraw, rgnGetUniq (pScreen->sb.prgnRedraw));
                }

                if (pScreen->sb.fMSBDisabled)
                {
                    /* Enable MSBs again. */
                    SBLOG(("MSBCONTROL: enabling MSBs.\n"));
                    pScreen->sb.fMSBDisabled = false;
                }
            }
        }
    }

    sbUnlock ();
}

void shadowBufferRegisterVideoHandler(VHCONTEXT *pVideoHandler)
{
    SBLOG(("pvideoHandler %p\n", pVideoHandler));
    if (sbLock(~0U))
    {
        g_pCtx->pVideoHandler = pVideoHandler;
        sbUnlock();
    }
}

void shadowBufferUnregisterVideoHandler(VHCONTEXT *pVideoHandler)
{
    RT_NOREF1(pVideoHandler);
    SBLOG(("pvideoHandler %p\n", pVideoHandler));
    if (sbLock(~0U))
    {
        Assert(g_pCtx->pVideoHandler == pVideoHandler);
        g_pCtx->pVideoHandler = NULL;
        sbUnlock();
    }
}

VHCONTEXT *shadowBufferGetVideoHandler(void)
{
    return g_pCtx->pVideoHandler;
}

/*
 * An internal shadow buffer handle consists from a generation number, which is
 * increased on every shadow buffer resize and an index value specified by the user.
 */
static SBHANDLE sbHandleCreate(VRDPSBSCREEN *pScreen, uint32_t u32Index)
{
    Assert(sbLocked());
    return SB_HANDLE_MAKE(pScreen->u32AccessKey, u32Index);
}

static bool sbHandleIsValid(VRDPSBSCREEN *pScreen, SBHANDLE handle)
{
    Assert(sbLocked());
    return    (handle == SB_HANDLE_NULL)
           || (pScreen->u32AccessKey == SB_HANDLE_GEN(handle));
}

/*
 * Shadow buffer supports "covers" which is a list of rectangles,
 * which no bitmap updates are generated for.
 */
SBHANDLE shadowBufferCoverAdd(unsigned uScreenId, const RGNRECT *pRect)
{
    SBLOG(("add cover rect %d,%d %dx%d\n", pRect->x, pRect->y, pRect->w, pRect->h));

    SBHANDLE handle = SB_HANDLE_NULL;

    if (sbLock(~0U))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            unsigned i;
            for (i = 0; i < RT_ELEMENTS(pScreen->aCovers); i++)
            {
                SBCOVERAREA *p = &pScreen->aCovers[i];

                if (p->handle == SB_HANDLE_NULL)
                {
                    p->handle = sbHandleCreate(pScreen, i);
                    p->rect = *pRect;

                    handle = p->handle;
                    break;
                }
            }
        }

        sbUnlock();
    }

    SBLOG(("add cover rect handle 0x%llx\n", handle));
    return handle;
}

void shadowBufferCoverRemove(unsigned uScreenId, SBHANDLE handle)
{
    SBLOG(("remove cover rect handle 0x%llx\n", handle));

    if (handle == SB_HANDLE_NULL)
    {
        return;
    }

    if (sbLock(~0U))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            if (sbHandleIsValid(pScreen, handle))
            {
                uint32_t index = SB_HANDLE_INDEX(handle);

                if (index < RT_ELEMENTS(pScreen->aCovers))
                {
                    SBCOVERAREA *p = &pScreen->aCovers[index];
                    if (p->handle == handle)
                    {
                        p->handle = SB_HANDLE_NULL;

                        /** @todo order.rect and no rect affected. */
                        VRDPORDERINTERNALREDRAW order;
                        order.u32Reserved = 0;
                        createMSB (&pScreen->sb, VRDP_ORDER_INTERNALREDRAW, &p->rect, &order, sizeof(order), true);
                    }
                    else
                    {
                        AssertMsgFailed(("invalid handle for %d: 0x%llx, actual 0x%llx\n", index, handle, p->handle));
                    }
                }
                else
                {
                    AssertMsgFailed(("index %d\n", index));
                }
            }
        }

        sbUnlock();
    }

    return;
}

void shadowBufferCoverResetAll(void)
{
    SBLOG(("\n"));

    if (sbLock(~0U))
    {
        unsigned uScreenId;

        for (uScreenId = 0; uScreenId < g_pCtx->cScreens; uScreenId++)
        {
            VRDPSBSCREEN *pScreen = sbResolveScreenId(uScreenId);

            if (pScreen)
            {
                memset(pScreen->aCovers, 0, sizeof (pScreen->aCovers));
            }
        }

        sbUnlock();
    }

    return;
}

typedef struct VIDEOFRAMEREADCONTEXT
{
    VRDPSBSCREEN *pScreen;
    VRDPTRANSBITSRECT *pTransRect;
} VIDEOFRAMEREADCONTEXT;

#if 0 /* unused */

static uint32_t getTrueColorPixel24(const uint8_t *pu8SrcLine, int x)
{
    uint32_t pixel = getpixel24(pu8SrcLine, x);
    uint32_t pixelRGB;
    pixelRGB = pixel & 0xFFFFFF;
    return pixelRGB;
}

static uint32_t getTrueColorPixel16(const uint8_t *pu8SrcLine, int x)
{
    uint32_t pixel = getpixel16(pu8SrcLine, x);
    uint32_t pixelRGB;
    ConvertColors ((uint8_t *)&pixel, 16, 1, (uint8_t *)&pixelRGB, 32, sizeof(pixelRGB));
    return pixelRGB;
}

static uint32_t getTrueColorPixel15(const uint8_t *pu8SrcLine, int x)
{
    uint32_t pixel = getpixel16(pu8SrcLine, x);
    uint32_t pixelRGB;
    ConvertColors ((uint8_t *)&pixel, 15, 1, (uint8_t *)&pixelRGB, 32, sizeof(pixelRGB));
    return pixelRGB;
}

static uint32_t getTrueColorPixel8(const uint8_t *pu8SrcLine, int x)
{
    uint32_t pixel = getpixel8(pu8SrcLine, x);
    uint32_t pixelRGB;
    ConvertColors ((uint8_t *)&pixel, 8, 1, (uint8_t *)&pixelRGB, 32, sizeof(pixelRGB));
    return pixelRGB;
}

#endif /* unsed */

static DECLCALLBACK(int) sbvsBitmapRead (void *pvCtx, uint8_t *pu8Buffer, uint32_t cbBuffer)
{
    VIDEOFRAMEREADCONTEXT *pCtx = (VIDEOFRAMEREADCONTEXT *)pvCtx;
    VRDPTRANSBITSRECT *pTransRect = pCtx->pTransRect;

    AssertReturn(cbBuffer >= pTransRect->rect.w * 4 *pTransRect->rect.h, VERR_INVALID_PARAMETER);

    if (RT_LIKELY(pTransRect->cBitsPerPixel == 32))
    {
        /* No color depth convertion. */
        if (RT_LIKELY(pCtx->pScreen->sb.transform.uTransform == VRDP_TRANSFORM_ROTATE_0))
        {
            /* Optimization for the usual "32 BPP. no transform" case. */
            if (RT_LIKELY(pTransRect->iDeltaLine == (int)(pTransRect->rect.w * 4)))
            {
                memcpy(pu8Buffer, pTransRect->pu8Src, pTransRect->iDeltaLine * pTransRect->rect.h);
            }
            else
            {
                const uint8_t *pu8SrcRow = pTransRect->pu8Src;
                uint8_t *pu8DstRow = pu8Buffer;

                int iDeltaLineDst = pTransRect->rect.w * 4;

                uint32_t y;
                for (y = 0; y < pTransRect->rect.h; y++)
                {
                    memcpy(pu8DstRow, pu8SrcRow, iDeltaLineDst);

                    pu8SrcRow += pTransRect->iDeltaLine;
                    pu8DstRow += iDeltaLineDst;
                }

                Assert(pu8DstRow == pu8Buffer + cbBuffer);
            }
        }
        else
        {
            /* Generic */
            /** @todo transrect pfnCopyRect/pfnCopyRectColor */
            const uint8_t *s = pTransRect->pu8Src;

            uint8_t *d = pu8Buffer;

            int x, y;

            const uint8_t *pu8SrcRow = s;
            uint8_t *pu8DstRow = d;

            for (y = 0; y < (int)pTransRect->rect.h; y++)
            {
                const uint8_t *pu8SrcLine = pu8SrcRow;
                uint8_t *pu8DstLine = pu8DstRow;

                for (x = 0; x < (int)pTransRect->rect.w; x++)
                {
                    unsigned pixel = pTransRect->pfnGetPixel (pu8SrcLine, 0);
                    pu8DstLine = pTransRect->pfnStorePixel (pu8DstLine, pixel);

                    pu8SrcLine += pTransRect->iDeltaPixel;
                }

                pu8SrcRow += pTransRect->iDeltaLine;
                pu8DstRow = pu8DstLine;
            }

            Assert(pu8DstRow == pu8Buffer + cbBuffer);
        }
    }
    else
    {
        /* Color convertion required. */
        const uint8_t *pu8SrcRow = pTransRect->pu8Src;
        uint8_t *pu8DstRow = pu8Buffer;

        unsigned y;
        for (y = 0; y < pTransRect->rect.h; y++)
        {
            ConvertColors (pu8SrcRow, pTransRect->cBitsPerPixel, pTransRect->rect.w,
                           pu8DstRow, 32, pTransRect->rect.w * 4);

            pu8SrcRow += pTransRect->iDeltaLine;
            pu8DstRow += pTransRect->rect.w * 4;
        }

        Assert(pu8DstRow == pu8Buffer + cbBuffer);
    }

    return VINF_SUCCESS;
}

/** @thread any */
void shadowBufferVideoDetectorCmd(uint8_t u8VDCmd)
{
    if (!sbLock (~0U))
    {
        return;
    }

    unsigned uScreenId;
    for (uScreenId = 0; uScreenId < g_pCtx->cScreens; uScreenId++)
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId(uScreenId);

        if (pScreen)
        {
            videoDetectorStoreCommand(pScreen->pVideoDetector, u8VDCmd);
        }
    }

    sbUnlock ();
}

/* Copy the rectangle from framebuffer to shadow buffer/MSBs.
 * Since this is called on the VM thread, the framebuffer
 * does not have to be locked. When the framebuffer is changed
 * the change is also syncronously processed on the VM thread
 * by the shadowBufferResize under the sbLock. And the output
 * thread does not touch the framebuffer.
 *
 * VM thread
 */
void shadowBufferBitmapUpdate (unsigned uScreenId, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    SBLOG(("Enter: %d,%d, %dx%d uScreenId = %d\n", x, y, w, h, uScreenId));

    /* Copy bits to pixel buffers under the sbLock. */
    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            RGNRECT rect;

            rect.x = x;
            rect.y = y;
            rect.w = w;
            rect.h = h;

            sbAdjustCoords (&rect, pScreen->sb.transform.cFBWidth, pScreen->sb.transform.cFBHeight);

            SBLOG(("%d,%d\n", w, h));

            if (w > 0 && h > 0)
            {
                /* Transform the framebuffer rectangle to the shadow buffer coordinates. */
                VRDEDATABITS bitsHdr;

                bitsHdr.cb       = 0;
                bitsHdr.x        = (int16_t)rect.x;
                bitsHdr.y        = (int16_t)rect.y;
                bitsHdr.cWidth   = (uint16_t)rect.w;
                bitsHdr.cHeight  = (uint16_t)rect.h;
                bitsHdr.cbPixel  = pScreen->sb.pixelBuffer.bytesPerPixel;

                uint8_t *pu8Src = pScreen->sb.src + pScreen->sb.srcLineSize * rect.y + pScreen->sb.pixelBuffer.bytesPerPixel * rect.x;

                VRDPTRANSBITSRECT transRect;
                pScreen->sb.transform.pfnTransformDataBits (&transRect, &bitsHdr, pu8Src, pScreen->sb.srcLineSize, &pScreen->sb.transform);

                bool fVideo = false;
                if (g_pCtx->pVideoHandler)
                {
                    RGNRECT rectUpdate;

                    rectUpdate.x = bitsHdr.x;
                    rectUpdate.y = bitsHdr.y;
                    rectUpdate.w = bitsHdr.cWidth;
                    rectUpdate.h = bitsHdr.cHeight;

                    RGNRECT rectVideo;
                    uint32_t u32VideoStreamId = 0;

                    uint64_t u64Timestamp = VRDPTimeMilliTS();

                    VIDEOFRAMEREADCONTEXT ctx;
                    ctx.pScreen = pScreen;
                    ctx.pTransRect = &transRect;

                    fVideo = videoDetectorBitmapUpdate(pScreen->pVideoDetector,
                                                       &rectUpdate,
                                                       &rectVideo,
                                                       u64Timestamp,
                                                       &u32VideoStreamId);

                    /* fVideo == true && u32VideoStreamId == 0 means ignore the frame. */
                    if (fVideo && u32VideoStreamId)
                    {
                        /* Report the frame to the external video stream handler, which
                         * will have to use a provided callback to access the frame bitmap.
                         * Prepare the callback context.
                         */
                        fVideo = videoHandlerSourceFrame(g_pCtx->pVideoHandler,
                                                         u32VideoStreamId,
                                                         u64Timestamp,
                                                         &rectUpdate,
                                                         sbvsBitmapRead, &ctx);
                    }
                }

                /* Always call this, because video channel adds a covered area, when video is actually played.
                 * and if this bitmap update was a video frame, it will be covered and will do nothing.
                 */
                sbCopyBitsToPixelBuffers (pScreen, &transRect);
            }
        }

        sbUnlock ();
    }

    SBLOG(("Leave\n"));
}

void shadowBufferBitmapUpdateEx (unsigned uScreenId,
                                 int32_t x, int32_t y, uint32_t w, uint32_t h,
                                 const uint8_t *pu8Bits,
                                 int32_t iDeltaLine,
                                 bool fVideoDetection)
{
    SBLOG(("Enter: %d,%d, %dx%d %d uScreenId = %d\n", x, y, w, h, iDeltaLine, uScreenId));

    /* Copy bits to pixel buffers under the sbLock. */
    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            RGNRECT rect;

            rect.x = x;
            rect.y = y;
            rect.w = w;
            rect.h = h;

            sbAdjustCoords (&rect, pScreen->sb.transform.cFBWidth, pScreen->sb.transform.cFBHeight);

            SBLOG(("%d,%d\n", w, h));

            if (w > 0 && h > 0)
            {
                /* Transform the framebuffer rectangle to the shadow buffer coordinates. */
                VRDEDATABITS bitsHdr;

                bitsHdr.cb       = 0;
                bitsHdr.x        = (int16_t)rect.x;
                bitsHdr.y        = (int16_t)rect.y;
                bitsHdr.cWidth   = (uint16_t)rect.w;
                bitsHdr.cHeight  = (uint16_t)rect.h;
                bitsHdr.cbPixel  = pScreen->sb.pixelBuffer.bytesPerPixel;

                const uint8_t *pu8Src = pu8Bits;

                VRDPTRANSBITSRECT transRect;
                pScreen->sb.transform.pfnTransformDataBits (&transRect,
                                                            &bitsHdr, pu8Src,
                                                            iDeltaLine, &pScreen->sb.transform);

                bool fVideo = false;
                if (fVideoDetection && g_pCtx->pVideoHandler)
                {
                    RGNRECT rectUpdate;

                    rectUpdate.x = bitsHdr.x;
                    rectUpdate.y = bitsHdr.y;
                    rectUpdate.w = bitsHdr.cWidth;
                    rectUpdate.h = bitsHdr.cHeight;

                    RGNRECT rectVideo;
                    uint32_t u32VideoStreamId = 0;

                    uint64_t u64Timestamp = VRDPTimeMilliTS();

                    VIDEOFRAMEREADCONTEXT ctx;
                    ctx.pScreen = pScreen;
                    ctx.pTransRect = &transRect;

                    fVideo = videoDetectorBitmapUpdate(pScreen->pVideoDetector,
                                                       &rectUpdate,
                                                       &rectVideo,
                                                       u64Timestamp,
                                                       &u32VideoStreamId);

                    /* fVideo == true && u32VideoStreamId == 0 means ignore the frame. */
                    if (fVideo && u32VideoStreamId)
                    {
                        /* Report the frame to the external video stream handler, which
                         * will have to use a provided callback to access the frame bitmap.
                         * Prepare the callback context.
                         */
                        fVideo = videoHandlerSourceFrame(g_pCtx->pVideoHandler,
                                                         u32VideoStreamId,
                                                         u64Timestamp,
                                                         &rectUpdate,
                                                         sbvsBitmapRead, &ctx);
                    }
                }

                /* Always call this, because video channel adds a covered area, when video is actually played.
                 * and if this bitmap update was a video frame, it will be covered and will do nothing.
                 */
                sbCopyBitsToPixelBuffers (pScreen, &transRect);
            }
        }

        sbUnlock ();
    }

    SBLOG(("Leave\n"));
}

void shadowBufferQueryRect (unsigned uScreenId, RGNRECT *prect)
{
    SBLOG(("Enter: uScreenId = %d\n", uScreenId));

    memset (prect, 0, sizeof (RGNRECT));

    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            *prect = pScreen->sb.pixelBuffer.rect;
        }

        sbUnlock ();
    }
}

bool shadowBufferAreOrderCoordsValid(unsigned uScreenId, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    SBLOG(("Enter: uScreenId = %d\n", uScreenId));

    bool fValid = false;

    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            RGNRECT rect;
            rect.x = x;
            rect.y = y;
            rect.w = w;
            rect.h = h;

            RGNRECT rectFB;
            rectFB.x = 0;
            rectFB.y = 0;
            rectFB.w = pScreen->sb.transform.cFBWidth;
            rectFB.h = pScreen->sb.transform.cFBHeight;

            fValid = rgnIsRectWithin(&rectFB, &rect);
        }

        sbUnlock ();
    }

    return fValid;
}


void shadowBufferTransformRect (unsigned uScreenId, RGNRECT *prect)
{
    SBLOG(("Enter: uScreenId = %d\n", uScreenId));

    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            pScreen->sb.transform.pfnTransformRect (prect, pScreen->sb.transform.cSBWidth, pScreen->sb.transform.cSBHeight);
        }

        sbUnlock ();
    }
}

void shadowBufferTransformRectGeneric (unsigned uScreenId, RGNRECT *prect, unsigned w, unsigned h)
{
    SBLOG(("Enter: uScreenId = %d\n", uScreenId));

    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            /* Both the rectangle and the w,h are in the FB coordinate system.
             * First convert the w,h so the TransformRect will get the
             * SB w,h target dimensions.
             */
            pScreen->sb.transform.pfnTransformWidthHeight (&w, &h);

            pScreen->sb.transform.pfnTransformRect (prect, w, h);
        }

        sbUnlock ();
    }
}

void shadowBufferTransformWidthHeight(unsigned uScreenId, unsigned *pw, unsigned *ph)
{
    SBLOG(("Enter: uScreenId = %d\n", uScreenId));

    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            pScreen->sb.transform.pfnTransformWidthHeight (pw, ph);
        }

        sbUnlock ();
    }
}

void shadowBufferTransformPoint (unsigned uScreenId, int *px, int *py)
{
    SBLOG(("Enter: uScreenId = %d\n", uScreenId));

    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            pScreen->sb.transform.pfnTransformPoint (px, py, pScreen->sb.transform.cSBWidth, pScreen->sb.transform.cSBHeight);
        }

        sbUnlock ();
    }
}

void shadowBufferTransformPointToFB (unsigned uScreenId, int *px, int *py)
{
    SBLOG(("Enter: uScreenId = %d\n", uScreenId));

    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            pScreen->sb.transform.pfnTransformPointToFB (px, py, pScreen->sb.transform.cFBWidth, pScreen->sb.transform.cFBHeight);
        }

        sbUnlock ();
    }
}

void shadowBufferRedrawUpdate (unsigned uScreenId, const RGNRECT *pRectScreen, const RGNRECT *pRectClient)
{
    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            if (pRectClient)
            {
                VRDPORDERINTERNALCLS order;

                order.x = pRectClient->x;
                order.y = pRectClient->y;
                order.w = pRectClient->w;
                order.h = pRectClient->h;

                /* pRect parameter is NULL in createMSB, because the CLS will be immediately followed by an update. */
                createMSB (&pScreen->sb, VRDP_ORDER_INTERNALCLS, NULL, &order, sizeof (order), true);
            }

            if (pRectScreen == NULL)
            {
                /* Fullscreen update requested. */
                pRectScreen = &pScreen->sb.pixelBuffer.rect;
            }

            if (pRectScreen && !rgnIsRectEmpty (pRectScreen))
            {
                /* Copy bits to pixel buffers under the sbLock. */
                /* The pScreenRect is in the shadow buffer coordinates,
                 * they have to be converted back to framebuffer ones.
                 */
                RGNRECT rect = *pRectScreen;

                sbAdjustCoords (&rect, pScreen->sb.transform.cFBWidth, pScreen->sb.transform.cFBHeight);

                pScreen->sb.transform.pfnTransformRectToFB (&rect, pScreen->sb.transform.cFBWidth, pScreen->sb.transform.cFBHeight);

                VRDEDATABITS bitsHdr;

                bitsHdr.cb       = 0; /* unused */
                bitsHdr.x        = (int16_t)rect.x;
                bitsHdr.y        = (int16_t)rect.y;
                bitsHdr.cWidth   = (uint16_t)rect.w;
                bitsHdr.cHeight  = (uint16_t)rect.h;
                bitsHdr.cbPixel  = pScreen->sb.pixelBuffer.bytesPerPixel;

                uint8_t *pu8Src = pScreen->sb.src + pScreen->sb.srcLineSize * rect.y + pScreen->sb.pixelBuffer.bytesPerPixel * rect.x;

                VRDPTRANSBITSRECT transRect;
                pScreen->sb.transform.pfnTransformDataBits (&transRect, &bitsHdr, pu8Src, pScreen->sb.srcLineSize, &pScreen->sb.transform);

                /* The convertion is identical. */
                Assert (   rect.x == transRect.rect.x && rect.y == transRect.rect.y
                        && rect.w == transRect.rect.w && rect.h == transRect.rect.h);

                /* Disable rectangle adjustments. */
                bool fAdjustRects = pScreen->sb.fAdjustRects;
                pScreen->sb.fAdjustRects = false;

                sbCopyBitsToPixelBuffers (pScreen, &transRect);

                /* Restore rectangle adjustments. */
                pScreen->sb.fAdjustRects = fAdjustRects;
            }
        }

        sbUnlock ();
    }
}

bool shadowBufferTransformDataBits (unsigned uScreenId, VRDEDATABITS *pTransBitsHdr, const uint8_t **ppu8TransBits, const VRDEDATABITS *pBitsHdr, const uint8_t *pu8Bits)
{
    SBLOG(("Enter: uScreenId = %d\n", uScreenId));

    bool fResult = false;

    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            if (pScreen->sb.transform.uTransform == VRDP_TRANSFORM_ROTATE_0)
            {
                /* Optimization. */
                *pTransBitsHdr = *pBitsHdr;
                *ppu8TransBits = pu8Bits;
                fResult = true;
            }
            else
            {
                unsigned cbLine = pBitsHdr->cWidth * pBitsHdr->cbPixel;

                VRDPTRANSBITSRECT transRect;
                pScreen->sb.transform.pfnTransformDataBits (&transRect, pBitsHdr, pu8Bits, cbLine, &pScreen->sb.transform);

                uint8_t *pu8DstBits = (uint8_t *)VRDPMemAlloc (cbLine * pBitsHdr->cHeight);

                if (pu8DstBits)
                {
                    /* Copy the source bits to the allocated buffer. Unoptimized generic version. */
                    int x, y;

                    const uint8_t *pu8SrcRow = transRect.pu8Src;
                    uint8_t *pu8DstRow = pu8DstBits;

                    unsigned cbDstLine = transRect.rect.w * transRect.cBytesPerPixel;

                    for (y = 0; y < (int)transRect.rect.h; y++)
                    {
                        const uint8_t *pu8SrcLine = pu8SrcRow;
                        uint8_t *pu8DstLine = pu8DstRow;

                        for (x = 0; x < (int)transRect.rect.w; x++)
                        {
                            unsigned pixel = transRect.pfnGetPixel (pu8SrcLine, 0);
                            pu8DstLine = transRect.pfnStorePixel (pu8DstLine, pixel);

                            pu8SrcLine += transRect.iDeltaPixel;
                        }

                        pu8SrcRow += transRect.iDeltaLine;
                        pu8DstRow += cbDstLine;
                    }

                    /* Update output parameters. */
                    *ppu8TransBits = pu8DstBits;

                    pTransBitsHdr->cb       = pBitsHdr->cb;
                    pTransBitsHdr->x        = (int16_t)transRect.rect.x;
                    pTransBitsHdr->y        = (int16_t)transRect.rect.y;
                    pTransBitsHdr->cWidth   = (uint16_t)transRect.rect.w;
                    pTransBitsHdr->cHeight  = (uint16_t)transRect.rect.h;
                    pTransBitsHdr->cbPixel  = pBitsHdr->cbPixel;

                    fResult = true;
                }
            }
        }

        sbUnlock ();
    }

    return fResult;
}

void shadowBufferFreeTransformDataBits (unsigned uScreenId, VRDEDATABITS *pTransBitsHdr, const uint8_t *pu8TransBits, const VRDEDATABITS *pBitsHdr, const uint8_t *pu8Bits)
{
    NOREF(uScreenId);
    NOREF(pTransBitsHdr);
    NOREF(pBitsHdr);

    if (pu8TransBits != pu8Bits)
    {
        VRDPMemFree ((void *)pu8TransBits);
    }
}

static bool sbRedrawCoveredOrder(VRDPSBSCREEN *pScreen, const RGNRECT *prectOrder)
{
    bool fCovered = false;

    unsigned i;
    for (i = 0; i < RT_ELEMENTS(pScreen->aCovers); i++)
    {
        if (pScreen->aCovers[i].handle != SB_HANDLE_NULL)
        {
            RGNRECT rectResult;

            if (rgnIntersectRects(&rectResult,
                                  prectOrder,
                                  &pScreen->aCovers[i].rect))
            {
                fCovered = true;
                break;
            }
        }
    }

    if (fCovered)
    {
        rgnAddRect (pScreen->sb.prgnRedraw, prectOrder);
    }

    return fCovered;
}

/* Private helpers for parsing of orders. Used only in shadowBufferOrder. Then is undefined. */
#define VBVA_GET_FIELD(__ptr, __t, __src, __remaining, __txt)                      \
    SBLOG(("fetching %d, remaining %d\n", sizeof (__t), __remaining));             \
    pu8SrcNext = (__src) + sizeof (__t);                                           \
    if (   (uintptr_t)pu8SrcNext > (uintptr_t)pu8SrcEnd                            \
        || (uintptr_t)pu8SrcNext < (uintptr_t)pu8SrcFirst)                         \
    {                                                                              \
        SBLOG(("Failed: 0x%x bytes [%s]: %p [%p; %p)!!!\n", sizeof(__t), (__txt), pu8SrcNext, pu8SrcFirst, pu8SrcEnd)); \
        sbUnlock ();                                                               \
        return;                                                                    \
    }                                                                              \
                                                                                   \
    __ptr = (__t *)(__src);                                                        \
    __src = pu8SrcNext;                                                            \
    __remaining -= sizeof (__t);

#define VRDP_CASE_ORDER(__t)                                                                      \
    case VRDE_ORDER_##__t:                                                                        \
    {                                                                                             \
        VRDEORDER##__t *pOrder;                                                                   \
        SBLOG(("VRDE_ORDER_" #__t "\n"));                                                         \
        VBVA_GET_FIELD(pOrder, VRDEORDER##__t, pu8Src, cbSrcRemaining, "VRDEORDER" #__t)          \
                                                                                                  \
        if (!fSeqSkip)                                                                            \
        {                                                                                         \
            fProcessed = createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, sizeof (*pOrder), false); \
        }                                                                                         \
    } break;

#define VRDP_CASE_ORDER_INTERNAL(__t)                                                             \
    case (uint32_t)VRDP_ORDER_##__t:                                                              \
    {                                                                                             \
        VRDPORDER##__t *pOrder;                                                                   \
        SBLOG(("VRDP_ORDER_" #__t "\n"));                                                         \
        VBVA_GET_FIELD(pOrder, VRDPORDER##__t, pu8Src, cbSrcRemaining, "VRDPORDER" #__t)          \
                                                                                                  \
        if (!fSeqSkip)                                                                            \
        {                                                                                         \
            fProcessed = createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, sizeof (*pOrder), false); \
        }                                                                                         \
    } break;

enum SeqStatus
{
    SEQ_NOTHING = 0,
    SEQ_BOUNDS,
    SEQ_ORDER,
    SEQ_REPEAT
};

/* VM thread */
void shadowBufferOrder (unsigned uScreenId, void *pdata, uint32_t cbdata)
{
    SBLOG(("uScreenId = %d, pdata = %p, cbdata = %d\n", uScreenId, pdata, cbdata));

    Assert(cbdata > 0); /* This is ensured by caller. */

    /* Local vars to control buffer bounds. */
    uint8_t *pu8SrcFirst = (uint8_t *)pdata;
    uint8_t *pu8SrcEnd = pu8SrcFirst + cbdata;
    uint8_t *pu8SrcNext = NULL; /* used in VBVA_GET_FIELD */
    int32_t i32LastOp = -1;
    uint8_t *pu8LastSrc = NULL;
    uint32_t cbLastSrcRemaining = 0;

    uint8_t *pu8Src = (uint8_t *)pdata;
    uint32_t cbSrcRemaining = cbdata;

    if (sbLock (uScreenId))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen == NULL)
        {
            sbUnlock();
            return;
        }

        /* Extract affected coords. */
        VBVACMDHDR *pHdr;
        VBVA_GET_FIELD(pHdr, VBVACMDHDR, pu8Src, cbSrcRemaining, "VBVACMDHDR")

        Assert (cbSrcRemaining);

        /* Convert affected rectangle to the SB coords. */
        RGNRECT rectAffected;

        rectAffected.x = pHdr->x;
        rectAffected.y = pHdr->y;
        rectAffected.w = pHdr->w;
        rectAffected.h = pHdr->h;

        /* Verify rectAffected, especially w = -1 h = -1. The rectangle must be within shadow buffer. */
        if (!rgnIsRectWithin(&pScreen->sb.pixelBuffer.rect, &rectAffected))
        {
            static int scLogged = 0;
            if (scLogged < 32)
            {
                scLogged++;
                LogRel(("VRDP: Update %d,%d %dx%d is out of range %dx%d\n",
                        rectAffected.x, rectAffected.y, rectAffected.w, rectAffected.h,
                        pScreen->sb.pixelBuffer.rect.w, pScreen->sb.pixelBuffer.rect.h
                      ));
            }
            sbUnlock();
            return;
        }

        pScreen->sb.transform.pfnTransformRect (&rectAffected, pScreen->sb.transform.cSBWidth, pScreen->sb.transform.cSBHeight);

        /* Orders may come either as a single VRDE_ORDER_* or as a
         * VRDE_ORDER_BOUNDS VRDE_ORDER_* VRDE_ORDER_REPEAT sequence.
         * So when a sequence is processed and a MSB is not created for any element,
         * then a redraw with rectAffected must be generated and the remaining
         * elements must be ignored.
         */
        uint32_t u32SeqStatus = SEQ_NOTHING;
        bool fSeqSkip = false; /* if true then skip remaining elements of the sequence. */

        while (cbSrcRemaining)
        {
            SBLOG(("remaining %d\n", cbSrcRemaining));

            if (   (uintptr_t)pu8Src >= (uintptr_t)pu8SrcEnd
                || (uintptr_t)pu8Src < (uintptr_t)pu8SrcFirst)
            {
                static int scLogged = 0;
                if (scLogged < 16)
                {
                    scLogged++;
                    LogRel(("VRDP: shadowBufferOrder: pointer %p/%d is out of range [%p; %p) after op %d, %p, %d\n",
                            pu8Src, cbSrcRemaining, pu8SrcFirst, pu8SrcEnd, i32LastOp, pu8LastSrc, cbLastSrcRemaining));
                }
                sbUnlock();
                return;
            }

            /* Extract the operation code. */
            uint32_t *pi32Op;
            VBVA_GET_FIELD(pi32Op, uint32_t, pu8Src, cbSrcRemaining, "Op code")

            SBLOG(("op 0x%08X\n", *pi32Op));

            i32LastOp = *pi32Op;
            pu8LastSrc = pu8Src;
            cbLastSrcRemaining = cbSrcRemaining;

            /* Process the status of BOUNDS ORDER REPEAT sequence. */
            switch (*pi32Op)
            {
                case VRDE_ORDER_BOUNDS:
                {
                    AssertLogRel(u32SeqStatus == SEQ_NOTHING || u32SeqStatus == SEQ_REPEAT);
                    u32SeqStatus = SEQ_BOUNDS;
                } break;

                case VRDE_ORDER_REPEAT:
                {
                    AssertLogRel(u32SeqStatus == SEQ_ORDER || u32SeqStatus == SEQ_REPEAT);
                    u32SeqStatus = SEQ_REPEAT;
                } break;

                default:
                {
                    if (u32SeqStatus == SEQ_NOTHING)
                    {
                        /* A new single order. Do nothing. */
                    }
                    else if (u32SeqStatus == SEQ_REPEAT)
                    {
                        /* A sequence has been completed. A new single order arrived. */
                        u32SeqStatus = SEQ_NOTHING;
                    }
                    else
                    {
                        AssertLogRel(u32SeqStatus == SEQ_BOUNDS);
                        u32SeqStatus = SEQ_ORDER;
                    }
                } break;
            }

            /* Check if the sequence state must be reset. */
            if (u32SeqStatus == SEQ_NOTHING || u32SeqStatus == SEQ_BOUNDS)
            {
                /* A new single order or sequence. */
                fSeqSkip = false;
            }

            /* Whether the order has been processed, that is not skipped. */
            bool fProcessed = false;

            switch (*pi32Op)
            {
                VRDP_CASE_ORDER_INTERNAL(INTERNALCLS)
                VRDP_CASE_ORDER(SOLIDBLT)
                VRDP_CASE_ORDER(PATBLTBRUSH)
                VRDP_CASE_ORDER(LINE)
                VRDP_CASE_ORDER(POLYLINE)
                VRDP_CASE_ORDER(ELLIPSE)

                case VRDE_ORDER_DSTBLT:
                {
                    VRDEORDERDSTBLT *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERDSTBLT, pu8Src, cbSrcRemaining, "VRDEORDERDSTBLT")

                    SBLOG(("VRDE_ORDER_DSTBLT %d,%d %dx%d 0x%02X\n",
                           pOrder->x, pOrder->y, pOrder->w, pOrder->h, pOrder->rop));

                    if (!fSeqSkip && !sbRedrawCoveredOrder(pScreen, &rectAffected))
                    {
                        fProcessed = createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, sizeof (*pOrder), false);
                    }

                    /* Must be after the sbRedrawCoveredOrder call to nake sure that the covered order is redrawn. */
                    videoDetectorOrderUpdate(pScreen->pVideoDetector, &rectAffected);
                } break;

                case VRDE_ORDER_SOLIDRECT:
                {
                    VRDEORDERSOLIDRECT *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERSOLIDRECT, pu8Src, cbSrcRemaining, "VRDEORDERSOLIDRECT")

                    SBLOG(("VRDE_ORDER_SOLIDRECT %d,%d %dx%d 0x%08X\n",
                           pOrder->x, pOrder->y, pOrder->w, pOrder->h, pOrder->rgb));
                    if (!fSeqSkip)
                    {
                        fProcessed = createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, sizeof (*pOrder), false);
                    }
                } break;

                case VRDE_ORDER_REPEAT:
                {
                    VRDEORDERREPEAT *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERREPEAT, pu8Src, cbSrcRemaining, "VRDEORDERREPEAT")

                    SBLOG(("VRDE_ORDER_REPEAT %d,%d %d,%d (%dx%d)\n",
                           pOrder->bounds.pt1.x, pOrder->bounds.pt1.y, pOrder->bounds.pt2.x, pOrder->bounds.pt2.y,
                           pOrder->bounds.pt2.x - pOrder->bounds.pt1.x, pOrder->bounds.pt2.y - pOrder->bounds.pt1.y));
                    if (!fSeqSkip)
                    {
                        fProcessed = createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, sizeof (*pOrder), false);
                    }
                } break;

                case VRDE_ORDER_BOUNDS:
                {
                    VRDEORDERBOUNDS *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERBOUNDS, pu8Src, cbSrcRemaining, "VRDEORDERBOUNDS")

                    SBLOG(("VRDE_ORDER_BOUNDS %d,%d %d,%d (%dx%d)\n",
                           pOrder->pt1.x, pOrder->pt1.y, pOrder->pt2.x, pOrder->pt2.y,
                           pOrder->pt2.x - pOrder->pt1.x, pOrder->pt2.y - pOrder->pt1.y));
                    AssertLogRel(!fSeqSkip);
                    fProcessed = createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, sizeof (*pOrder), false);
                } break;

                case VRDE_ORDER_MEMBLT:
                {
                    VRDEORDERMEMBLT *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERMEMBLT, pu8Src, cbSrcRemaining, "VRDEORDERMEMBLT")

                    SBLOG(("VRDE_ORDER_MEMBLT %d,%d %dx%d from %d,%d rop 0x%02X hash %08X %08X %08X %08X\n",
                           pOrder->x, pOrder->y, pOrder->w, pOrder->h, pOrder->xSrc, pOrder->ySrc, pOrder->rop,
                           *(uint32_t *)&pOrder->hash[0], *(uint32_t *)&pOrder->hash[4],
                           *(uint32_t *)&pOrder->hash[8], *(uint32_t *)&pOrder->hash[12]));
                    if (!fSeqSkip)
                    {
                        fProcessed = createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, sizeof (*pOrder), false);
                    }
                } break;

                case VRDE_ORDER_SCREENBLT:
                {
                    VRDEORDERSCREENBLT *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERSCREENBLT, pu8Src, cbSrcRemaining, "VRDEORDERSCREENBLT")

                    SBLOG(("VRDE_ORDER_SCREENBLT: %d,%d from %d,%d %dx%d\n",
                            pOrder->x, pOrder->y, pOrder->xSrc, pOrder->ySrc, pOrder->w, pOrder->h));

                    /* Recompute the area affected by the order. The area must include both target and source
                     * rectangles. While *pHdr is equal only to target rectangle.
                     */
                    VBVACMDHDR hdr;

                    hdr.x = RT_MIN (pOrder->x, pOrder->xSrc);
                    hdr.y = RT_MIN (pOrder->y, pOrder->ySrc);

                    hdr.w = pOrder->w + (hdr.x == pOrder->x?
                                            pOrder->xSrc - pOrder->x:
                                            pOrder->x - pOrder->xSrc);

                    hdr.h = pOrder->h + (hdr.y == pOrder->y?
                                            pOrder->ySrc - pOrder->y:
                                            pOrder->y - pOrder->ySrc);

                    /* Retransform affected rectangle. */
                    rectAffected.x = hdr.x;
                    rectAffected.y = hdr.y;
                    rectAffected.w = hdr.w;
                    rectAffected.h = hdr.h;
                    if (!rgnIsRectWithin(&pScreen->sb.pixelBuffer.rect, &rectAffected))
                    {
                        sbUnlock ();
                        return;
                    }

                    pScreen->sb.transform.pfnTransformRect (&rectAffected, pScreen->sb.transform.cSBWidth, pScreen->sb.transform.cSBHeight);

                    if (!fSeqSkip && !sbRedrawCoveredOrder(pScreen, &rectAffected))
                    {
                        fProcessed = createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, sizeof (*pOrder), false);
                    }

                    /* Must be after the sbRedrawCoveredOrder call to nake sure that the covered order is redrawn. */
                    videoDetectorOrderUpdate(pScreen->pVideoDetector, &rectAffected);
                } break;

                case VRDE_ORDER_CACHED_BITMAP:
                {
                    VRDEORDERCACHEDBITMAP *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERCACHEDBITMAP, pu8Src, cbSrcRemaining, "VRDEORDERCACHEDBITMAP")

                    SBLOG(("VRDE_ORDER_CACHED_BITMAP: hash %08X %08X %08X %08X\n",
                            *(uint32_t *)&pOrder->hash[0], *(uint32_t *)&pOrder->hash[4], *(uint32_t *)&pOrder->hash[8], *(uint32_t *)&pOrder->hash[12]));

                    /* Extract the operation header bitmap header. */
                    VRDEDATABITS *pBitsHdr;
                    VBVA_GET_FIELD(pBitsHdr, VRDEDATABITS, pu8Src, cbSrcRemaining, "VRDEDATABITS(CACHEDBITMAP)")

                    SBLOG(("VRDE_ORDER_CACHED_BITMAP: pBitsHdr cb = %d, x = %d, y = %d, cWidth = %d, cHeight = %d, cbPixel = %d, hash %08X %08X %08X %08X\n",
                            pBitsHdr->cb, pBitsHdr->x, pBitsHdr->y, pBitsHdr->cWidth, pBitsHdr->cHeight, pBitsHdr->cbPixel,
                            *(uint32_t *)&pOrder->hash[0], *(uint32_t *)&pOrder->hash[4], *(uint32_t *)&pOrder->hash[8], *(uint32_t *)&pOrder->hash[12]));

                    /* Verify that the buffer is big enough for those bits. */
                    if (pBitsHdr->cb > cbSrcRemaining)
                    {
                        VRDPLOGRELLIMIT(16, ("Size of bits (%d) exceeds the size of buffer (%d)!!! %d,%d %dx%d %d.\n",
                                        pBitsHdr->cb, cbSrcRemaining,
                                        pBitsHdr->x, pBitsHdr->y, pBitsHdr->cWidth, pBitsHdr->cHeight, pBitsHdr->cbPixel));
                        sbUnlock ();
                        return;
                    }

                    if (!isValidBitsHdr(pBitsHdr))
                    {
                        sbUnlock ();
                        return;
                    }

                    BCHEAPHANDLE hBmp;
                    int rc = BCStore(&hBmp, g_pCtx->pServer->BC(),
                                                VRDE_ORDER_CACHED_BITMAP,
                                                &pOrder->hash,
                                                sizeof (pOrder->hash),
                                                pBitsHdr,
                                                pu8Src,
                                                pScreen->u32AccessKey,
                                                (uint32_t)pScreen->uScreenId);

                    if (RT_SUCCESS(rc))
                    {
                        bool fCreated = createMSB (&pScreen->sb, *pi32Op, NULL, &hBmp, sizeof (hBmp), false);
                        if (!fCreated)
                        {
                            BCBitmapHeapBlockFree(g_pCtx->pServer->BC(), &hBmp);
                        }
                    }

                    cbSrcRemaining -= pBitsHdr->cb;
                    pu8Src += pBitsHdr->cb;

                    /* Single order, always considered as a processed. */
                    fProcessed = true;
                } break;

                case VRDE_ORDER_DELETED_BITMAP:
                {
                    VRDEORDERDELETEDBITMAP *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERDELETEDBITMAP, pu8Src, cbSrcRemaining, "VRDEORDERDELETEDBITMAP")

                    SBLOG(("VRDE_ORDER_DELETED_BITMAP: hash %08X %08X %08X %08X\n",
                            *(uint32_t *)&pOrder->hash[0], *(uint32_t *)&pOrder->hash[4], *(uint32_t *)&pOrder->hash[8], *(uint32_t *)&pOrder->hash[12]));

                    createMSB (&pScreen->sb, *pi32Op, NULL, pOrder, sizeof (*pOrder), true);

                    /* Single order, always considered as a processed. */
                    fProcessed = true;
                } break;

                case VRDE_ORDER_SAVESCREEN:
                {
                    VRDEORDERSAVESCREEN *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERSAVESCREEN, pu8Src, cbSrcRemaining, "VRDEORDERSAVESCREEN")

                    SBLOG(("VRDE_ORDER_SAVESCREEN: %d,%d %d,%d %d %d\n",
                            pOrder->pt1.x, pOrder->pt1.y, pOrder->pt2.x, pOrder->pt2.y, pOrder->ident, pOrder->restore));

                    if (pOrder->restore)
                    {
                        /* Bits follow the order. Extract the bitmap header. */
                        VRDEDATABITS *pBitsHdr;
                        VBVA_GET_FIELD(pBitsHdr, VRDEDATABITS, pu8Src, cbSrcRemaining, "VRDEDATABITS(SAVESCREEN)")

                        /* Verify that the buffer is big enough for those bits. */
                        if (pBitsHdr->cb > cbSrcRemaining)
                        {
                            SBLOG(("Size of bits (%d) exceeds the size of buffer (%d)!!!", pBitsHdr->cb, cbSrcRemaining));
                            sbUnlock ();
                            return;
                        }

                        SBLOG(("VRDE_ORDER_SAVESCREEN: pBitsHdr cb = %d, x = %d, y = %d, cWidth = %d, cHeight = %d, cbPixel = %d\n",
                                pBitsHdr->cb, pBitsHdr->x, pBitsHdr->y, pBitsHdr->cWidth, pBitsHdr->cHeight, pBitsHdr->cbPixel));

                        if (!isValidBitsHdr(pBitsHdr))
                        {
                            sbUnlock ();
                            return;
                        }

                        /* Save bitmap in the bmpcache intermediate heap, if that fails, do a bitmap update. */
                        BCHEAPHANDLE hBmp;
                        int rc = BCStore(&hBmp, g_pCtx->pServer->BC(),
                                                    VRDP_ORDER_INTERNAL_SAVESCREEN_RESTORE,
                                                    pOrder,
                                                    sizeof (*pOrder),
                                                    pBitsHdr,
                                                    pu8Src,
                                                    pScreen->u32AccessKey,
                                                    (uint32_t)pScreen->uScreenId);

                        if (RT_SUCCESS(rc))
                        {
                            bool fCreated = createMSB (&pScreen->sb, VRDP_ORDER_INTERNAL_SAVESCREEN_RESTORE, &rectAffected, &hBmp, sizeof (hBmp), false);
                            if (!fCreated)
                            {
                                BCBitmapHeapBlockFree(g_pCtx->pServer->BC(), &hBmp);
                            }
                        }
                        else
                        {
                            /* Copy bits to pixel buffers. */
                            uint32_t cbLine = pBitsHdr->cWidth * pBitsHdr->cbPixel;

                            VRDPTRANSBITSRECT transRect;
                            pScreen->sb.transform.pfnTransformDataBits (&transRect, pBitsHdr, pu8Src, cbLine, &pScreen->sb.transform);

                            sbCopyBitsToPixelBuffers (pScreen, &transRect);
                        }

                        cbSrcRemaining -= pBitsHdr->cb;
                        pu8Src += pBitsHdr->cb;
                    }
                    else
                    {
                        createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, sizeof (*pOrder), false);
                    }

                    /* Single order, always considered as a processed. */
                    fProcessed = true;
                } break;

                case VRDE_ORDER_DIRTY_RECT:
                {
                    /* This is an dirty rectangle update. Guest reports the raw bitmap data.
                     *
                     * Copy the bitmap directly to pixel buffers of the shadow buffer
                     * and of existing orders, splitting the bitmap rectangle if necessary.
                     * That way those dirty rects are accumulated without a need to hold
                     * them in memory.
                     */

                    /* Extract the bitmap header. */
                    VRDEDATABITS *pBitsHdr;
                    VBVA_GET_FIELD(pBitsHdr, VRDEDATABITS, pu8Src, cbSrcRemaining, "VRDEDATABITS")

                    SBLOG(("VRDE_ORDER_DIRTY_RECT: cb %d, %d,%d %dx%d, cbPixel %d\n",
                           pBitsHdr->cb, pBitsHdr->x, pBitsHdr->y, pBitsHdr->cWidth, pBitsHdr->cHeight, pBitsHdr->cbPixel));

                    /* Verify that the buffer is big enough for those bits. */
                    if (pBitsHdr->cb > cbSrcRemaining)
                    {
                        VRDPLOGRELLIMIT(16, ("Size of bits (%d) exceeds the size of buffer (%d)!!! %d,%d %dx%d %d.\n",
                                        pBitsHdr->cb, cbSrcRemaining,
                                        pBitsHdr->x, pBitsHdr->y, pBitsHdr->cWidth, pBitsHdr->cHeight, pBitsHdr->cbPixel));
                        sbUnlock ();
                        return;
                    }

                    if (!isValidBitsHdr(pBitsHdr))
                    {
                        sbUnlock ();
                        return;
                    }

                    /* Copy bits to pixel buffers. */
                    uint32_t cbLine = pBitsHdr->cWidth * pBitsHdr->cbPixel;

                    VRDPTRANSBITSRECT transRect;
                    pScreen->sb.transform.pfnTransformDataBits (&transRect, pBitsHdr, pu8Src, cbLine, &pScreen->sb.transform);

                    bool fVideo = false;
                    if (g_pCtx->pVideoHandler)
                    {
                        RGNRECT rectUpdate;

                        rectUpdate.x = pBitsHdr->x;
                        rectUpdate.y = pBitsHdr->y;
                        rectUpdate.w = pBitsHdr->cWidth;
                        rectUpdate.h = pBitsHdr->cHeight;

                        RGNRECT rectVideo;
                        uint32_t u32VideoStreamId = 0;

                        uint64_t u64Timestamp = VRDPTimeMilliTS();

                        VIDEOFRAMEREADCONTEXT ctx;
                        ctx.pScreen = pScreen;
                        ctx.pTransRect = &transRect;

                        fVideo = videoDetectorBitmapUpdate(pScreen->pVideoDetector,
                                                           &rectUpdate,
                                                           &rectVideo,
                                                           u64Timestamp,
                                                           &u32VideoStreamId);

                        /* fVideo == true && u32VideoStreamId == 0 means ignore the frame. */
                        if (fVideo && u32VideoStreamId)
                        {
                            /* Report the frame to the external video stream handler, which
                             * will have to use a provided callback to access the frame bitmap.
                             * Prepare the callback context.
                             */
                            fVideo = videoHandlerSourceFrame(g_pCtx->pVideoHandler,
                                                             u32VideoStreamId,
                                                             u64Timestamp,
                                                             &rectUpdate,
                                                             sbvsBitmapRead, &ctx);
                        }
                    }

                    /* Always call this, because video channel adds a covered area, when video is actually played.
                     * and if this bitmap update was a video frame, it will be covered and will do nothing.
                     */
                    sbCopyBitsToPixelBuffers (pScreen, &transRect);

                    cbSrcRemaining -= pBitsHdr->cb;
                    pu8Src += pBitsHdr->cb;

                    /* This does not involve MSBs so always considered as processed. */
                    fProcessed = true;
                } break;

                case VRDE_ORDER_TEXT:
                {
                    VRDEORDERTEXT *pOrder;
                    VBVA_GET_FIELD(pOrder, VRDEORDERTEXT, pu8Src, cbSrcRemaining, "VRDEORDERTEXT")

                    SBLOG(("VRDE_ORDER_TEXT: cb %d, bkground %d,%d %dx%d, opaque  %d,%d %dx%d, max %d, glyphs %d, flags %02X, charinc %d, fg %08X, bg %08X, cbOrder %d\n",
                            pOrder->cbOrder,
                            pOrder->xBkGround,
                            pOrder->yBkGround,
                            pOrder->wBkGround,
                            pOrder->hBkGround,
                            pOrder->xOpaque,
                            pOrder->yOpaque,
                            pOrder->wOpaque,
                            pOrder->hOpaque,
                            pOrder->u16MaxGlyph,
                            pOrder->u8Glyphs,
                            pOrder->u8Flags,
                            pOrder->u8CharInc,
                            pOrder->u32FgRGB,
                            pOrder->u32BgRGB,
                            pOrder->cbOrder

                         ));

                    if (pOrder->cbOrder > cbSrcRemaining + sizeof (VRDEORDERTEXT))
                    {
                        SBLOG(("Size of text order (%d) exceeds the size of buffer (%d)!!!", pOrder->cbOrder, cbSrcRemaining + sizeof (VRDEORDERTEXT)));
                        sbUnlock ();
                        return;
                    }

#ifdef LOG_ENABLED
                    VRDEORDERGLYPH *pGlyph = (VRDEORDERGLYPH *)pu8Src;

                    unsigned i;
                    for (i = 0; i < pOrder->u8Glyphs; i++)
                    {
                        SBLOG(("VRDEORDERGLYPH: [%d] next %d, handle 0x%llX, %d,%d %dx%d origin %d,%d\n",
                               i,
                               pGlyph->o32NextGlyph,
                               pGlyph->u64Handle,
                               pGlyph->x,
                               pGlyph->y,
                               pGlyph->w,
                               pGlyph->h,
                               pGlyph->xOrigin,
                               pGlyph->yOrigin
                             ));

                        unsigned cbRow = (pGlyph->w + 7)/8;

                        unsigned x, y;

                        for (y = 0; y < pGlyph->h; y++)
                        {
                            for (x = 0; x < cbRow; x++)
                            {
                                uint8_t u8 = pGlyph->au8Bitmap[y * cbRow + x];

                                VRDPLOG(("%c%c%c%c%c%c%c%c",
                                       (u8 & 0x80) != 0? '*': ' ',
                                       (u8 & 0x40) != 0? '*': ' ',
                                       (u8 & 0x20) != 0? '*': ' ',
                                       (u8 & 0x10) != 0? '*': ' ',
                                       (u8 & 0x8) != 0? '*': ' ',
                                       (u8 & 0x4) != 0? '*': ' ',
                                       (u8 & 0x2) != 0? '*': ' ',
                                       (u8 & 0x1) != 0? '*': ' '
                                     ));
                            }
                            VRDPLOG(("\n"));
                        }

                        pGlyph = (VRDEORDERGLYPH *)((uint8_t *)pGlyph + pGlyph->o32NextGlyph);
                    }
#endif /* LOG_ENABLED */

                    if (!fSeqSkip)
                    {
                        fProcessed = createMSB (&pScreen->sb, *pi32Op, &rectAffected, pOrder, pOrder->cbOrder, false);
                    }

                    cbSrcRemaining -= pOrder->cbOrder - sizeof (VRDEORDERTEXT);
                    pu8Src += pOrder->cbOrder - sizeof (VRDEORDERTEXT);
                } break;

                default:
                {
                    static int scLogged = 0;
                    if (scLogged < 16)
                    {
                        scLogged++;
                        LogRel(("VRDP: unsupported update 0x%08X %d,%d %dx%d\n",
                                *pi32Op, rectAffected.x, rectAffected.y, rectAffected.w, rectAffected.h
                              ));
                    }
                    SBLOG(("Unsupported op 0x%08X!!!\n", *pi32Op));
                    /* End the parsing loop. */
                    cbSrcRemaining = 0;
                } break;
            }

            /* Check whether a MSB was created and what is the current status of a sequence. */
            if (!fProcessed)
            {
                if (u32SeqStatus == SEQ_NOTHING)
                {
                    /* A new single order. Do nothing. */
                    SBLOG(("SEQ_NOTHING: no MSB\n"));
                    AssertLogRel(fSeqSkip == false);
                }
                else
                {
                    /* Redraw and skip the rest of the sequence. */
                    SBLOG(("SEQ_%d: no MSB, skip %d\n", u32SeqStatus, fSeqSkip));
                    if (!fSeqSkip)
                    {
                        rgnAddRect (pScreen->sb.prgnRedraw, &rectAffected);
                        fSeqSkip = true;
                    }
                }
            }
        }

        sbUnlock ();
    }
}

#undef VBVA_GET_FIELD
#undef VRDP_CASE_ORDER

static VRDPSBSCREEN *sbQueryOutputScreen (void)
{
    Assert (g_pCtx);
    VRDPSBSCREEN *pScreen = sbResolveScreenId(g_pCtx->uOutputScreenId);
    return pScreen;
}

static void sbAdvanceOutputScreen (void)
{
    Assert (g_pCtx);

    g_pCtx->uOutputScreenId++;

    if (g_pCtx->uOutputScreenId >= g_pCtx->cScreens)
    {
       g_pCtx->uOutputScreenId = 0;
    }
}

static void sbGetAction (VRDPSBSCREEN *pScreen, VrdpSbAct *pAction)
{
    int code = VRDP_SB_ACT_NOP;

    VRDPSHADOWBUFFER *psb = &pScreen->sb;

    pAction->uScreenId = pScreen->uScreenId;
    pAction->pvContext = NULL;

    if (psb->fResized)
    {
        psb->fResized = false;

        pAction->code = VRDP_SB_ACT_RESIZE;

        RESIZELOG(("return VRDP_SB_ACT_RESIZE.\n"));

        return;
    }

l_genAction:

    MICROSHADOWBUFFER *pmsb = psb->bottomMSB;

    /* Check if there is a pending order. We have to send the
     * saved region updates, then send the order and then send
     * updates accumulated in the order.
     */
    if (psb->fResetBounds)
    {
        SBLOG(("MSBLIMIT: reset bounds.\n"));
        pAction->u.order.pvOrder = NULL;
        pAction->u.order.cbOrder = 0;
        pAction->u.order.i32Op = VRDP_ORDER_INTERNALCANCEL;

        code = VRDP_SB_ACT_ORDER;

        psb->fResetBounds = false;
        pmsb = NULL; /* As if there was no MSB. */
    }
    else if (pmsb)
    {
        SBLOG(("pmsb = %p, pmsb->fUnderlayingDirtyAreaReported = %d, pmsb->prgnDirty = %p, pmsb->pvOrder = %p\n",
                pmsb, pmsb->fUnderlayingDirtyAreaReported, pmsb->prgnDirty, pmsb->pvOrder));

        if (!pmsb->fUnderlayingDirtyAreaReported)
        {
            SBLOG(("repoting underlaying area pmsb = %p.\n", pmsb));

            Assert (pmsb->prgnDirty == NULL);

            /* This is the first time the order is reported.
             * Check whether there are dirty bits under the region.
             */
            REGION *prgnSect = rgnCreateEmpty (psb->rgnCtx, pmsb->pb.rect.x, pmsb->pb.rect.y, pmsb->pb.rect.w, pmsb->pb.rect.h, pScreen->u32AccessKey);

            rgnCut (prgnSect, psb->pixelBuffer.prgnDirty, &pmsb->pb.rect);

            if (rgnIsEmpty (prgnSect))
            {
                SBLOG(("No dirty area under the order.\n"));
                rgnDelete (prgnSect);
            }
            else
            {
                SBLOG(("Dirty area under the order.\n"));
                pmsb->prgnDirty = prgnSect;

                /* Exclude the reported area from the shadow buffer dirty region. */
                rgnInvert (psb->pixelBuffer.prgnDirty);
                rgnAddRect (psb->pixelBuffer.prgnDirty, &pmsb->pb.rect);
                rgnInvert (psb->pixelBuffer.prgnDirty);
            }

            pmsb->fUnderlayingDirtyAreaReported = true;
        }

        if (pmsb->prgnDirty)
        {
            /* Report the not empty dirty region of the underlaying SB area.
             * That is pending updates arrived before the order.
             */
            pAction->u.region.prgn = pmsb->prgnDirty;

            pmsb->prgnDirty = NULL;

            code = VRDP_SB_ACT_REGION;

            SBLOG(("VRDP_SB_ACT_REGION underlaying\n"));
        }
        else if (pmsb->pvOrder)
        {
            SBLOG(("Reporting order pmsb = %p.\n", pmsb));

            /* Now the order itself is reported. */
            Assert (pmsb->pvOrder);

            pAction->u.order.pvOrder = pmsb->pvOrder;
            pAction->u.order.cbOrder = pmsb->cbOrder;
            pAction->u.order.i32Op = pmsb->i32Op;
            pAction->u.order.rectAffected = pmsb->rectAffected;

            code = VRDP_SB_ACT_ORDER;

            pmsb->pvOrder = NULL;

            /* If this is the last order after disabling MSBs and it's VRDE_ORDER_BOUNDS,
             * remember to reset bounds.
             */
            if (   psb->fMSBDisabled
                && psb->cMSBs == 1
                && pAction->u.order.i32Op == VRDE_ORDER_BOUNDS)
            {
                SBLOG(("MSBLIMIT: last order bounds.\n"));
                psb->fResetBounds = true;
            }

            SBLOG(("VRDP_SB_ACT_ORDER\n"));
        }
        else
        {
            SBLOG(("Deleting MSB %p.\n", pmsb));

            /* The order's dirty region now can be merged to
             * the shadow buffer.
             */
            if (!rgnIsEmpty (pmsb->pb.prgnDirty))
            {
                SBLOG(("pixel buffer dirty region is not empty pmsb = %p\n", pmsb));

                Assert (pmsb->pb.pu8Pixels);

                VRDPTRANSBITSRECT transRect;
                sbInitNormalTransBitsRect (&transRect, &pmsb->pb);

                sbCopyBitsToPixelBufferRgn (psb, &psb->pixelBuffer, pmsb->pb.prgnDirty,
                                            &transRect,
                                            false);

                rgnAdd (psb->pixelBuffer.prgnDirty, pmsb->pb.prgnDirty);

                SBLOG(("VRDP_SB_ACT_REGION: non empty dirty region for MSB %p.\n", pmsb));
            }

            /* The msb is not required anymore and can be removed. */
            pmsb->fOutput = false;

            deleteMSB (pmsb);
            pmsb = NULL;

            /* Now the action generation must be repeated. */
            goto l_genAction;
        }
    }
    else
    {
        /* No orders. Report the SB dirty region, if it is not empty. */
        if (!rgnIsEmpty (psb->pixelBuffer.prgnDirty))
        {
            pAction->u.region.prgn = psb->pixelBuffer.prgnDirty;

            /* Create new empty region which will accumulate new updates. */
            psb->pixelBuffer.prgnDirty = rgnCreateEmpty (psb->rgnCtx, 0, 0, psb->pixelBuffer.rect.w, psb->pixelBuffer.rect.h, pScreen->u32AccessKey);

            SBLOG(("VRDP_SB_ACT_REGION: no orders\n"));

            code = VRDP_SB_ACT_REGION;
        }
    }

    if (code == VRDP_SB_ACT_REGION)
    {
        rgnMergeAdjacentRows (pAction->u.region.prgn);
    }

    if (pmsb)
    {
        SB2LOG(("Marking msb %p as output\n", pmsb));
        pmsb->fOutput = true;
    }

    pAction->pvContext = pmsb;
    pAction->code = code;

    SB2LOG(("Leave code %d\n", code));

    return;
}

/* Output thread */
void shadowBufferGetAction (VrdpSbAct *pAction)
{
    SB2LOG(("Enter\n"));

    pAction->code = VRDP_SB_ACT_NOP;

    if (!sbLock (~0U))
    {
        return;
    }

    unsigned i;

    for (i = 0; i < g_pCtx->cScreens; i++)
    {
        VRDPSBSCREEN *pScreen = sbQueryOutputScreen ();

        SB2LOG(("i = %d, pScreen = %p\n", i, pScreen));

        if (pScreen)
        {
            sbGetAction (pScreen, pAction);

            if (pAction->code != VRDP_SB_ACT_NOP)
            {
                break;
            }
        }

        sbAdvanceOutputScreen ();
    }

    sbUnlock ();

    SB2LOG(("Leave code %d\n", pAction->code));
}

void shadowBufferCancelAction (VrdpSbAct *pAction)
{
    SB2LOG(("shadowBufferCancelAction %p\n", pAction));

    Assert (pAction);

    if (!sbLock (~0U))
    {
        pAction->code = VRDP_SB_ACT_NOP;
        return;
    }

    if (pAction->pvContext)
    {
        MICROSHADOWBUFFER *pmsb = (MICROSHADOWBUFFER *)pAction->pvContext;

        Assert (pmsb->fOutput);

        if (pmsb->fInList == false)
        {
            /* The msb was deleted during resize. */
            pmsb->fOutput = false;

            deleteMSB (pmsb);

            SBLOG(("postponed MSB %p deleted\n", pmsb));
        }
    }

    switch (pAction->code)
    {
        case VRDP_SB_ACT_NOP:
        {
            /* do nothing */
        } break;
        case VRDP_SB_ACT_REGION:
        {
            rgnDelete (pAction->u.region.prgn);
            pAction->u.region.prgn = (REGION *)(uintptr_t)0xDEADC0DE;
        } break;
        case VRDP_SB_ACT_ORDER:
        {
            pAction->u.order.pvOrder = NULL;
            pAction->u.order.cbOrder = 0;
            pAction->u.order.i32Op = 0;
        } break;
        case VRDP_SB_ACT_RESIZE:
        {
            /* do nothing */
        } break;
        default:
        {
            AssertMsgFailed(("%d\n", pAction->code));
        }
    }

    sbAdvanceOutputScreen ();

    sbUnlock ();

    pAction->code = VRDP_SB_ACT_NOP;

    SB2LOG(("shadowBufferCancelAction %p cancelled\n", pAction));

    return;
}

/* Output thread */
VRDPSBKEY shadowBufferBeginEnumRgnRect (REGION *prgn)
{
    SBLOG(("prgn = %p\n", prgn));
    Assert (prgn);

    VRDPSBKEY key = VRDP_SB_NULL_ACCESS_KEY;

    if (sbLock (~0U))
    {
        VRDPSBSCREEN *pScreen = sbQueryOutputScreen ();
        Assert(pScreen);

        if (pScreen)
        {
            SBLOG(("prgnUniq = %d, uniq = %d\n", rgnGetUniq (prgn), pScreen->u32AccessKey));

            if (rgnGetUniq (prgn) == pScreen->u32AccessKey)
            {
                /* The region was created for the screen. */
                rgnEnumRect (prgn);

                /* Return current access key, so the caller can use it for shadowBufferLock. */
                key = pScreen->u32AccessKey;
            }
        }

        sbUnlock ();
    }

    return key;
}

/* Output thread */
const RGNRECT *shadowBufferQueryNextRgnRect (REGION *prgn)
{
    SBLOG(("prgn = %p\n", prgn));
    Assert(prgn);

    const RGNRECT *prect = NULL;

    if (sbLock (~0U))
    {
        VRDPSBSCREEN *pScreen = sbQueryOutputScreen ();
        Assert(pScreen);

        if (pScreen)
        {
            if (rgnGetUniq (prgn) == pScreen->u32AccessKey)
            {
                prect = rgnNextRect (prgn);
            }
        }

        sbUnlock ();
    }

    return prect;
}

VRDPSBKEY shadowBufferLock (VRDPSBKEY key, VRDPBITSRECT *pBitsRect, unsigned uScreenId, const RGNRECT *pRect)
{
    if (!sbLock (uScreenId))
    {
        return VRDP_SB_NULL_ACCESS_KEY;
    }

    VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

    if (pScreen == NULL)
    {
        sbUnlock ();
        return VRDP_SB_NULL_ACCESS_KEY;
    }

    if (key != VRDP_SB_NULL_ACCESS_KEY)
    {
        /* Access requested to the same buffer. */
        if (key != pScreen->u32AccessKey)
        {
             sbUnlock ();
             return VRDP_SB_NULL_ACCESS_KEY;
        }
    }

#ifdef DEBUG
    pScreen->sb.cAccessed++;
#endif /* DEBUG */

    /* Now fill the requested information. */
    if (pRect == NULL)
    {
        /* Entire shadowbuffer was requested. */
        pBitsRect->rect = pScreen->sb.pixelBuffer.rect;
    }
    else
    {
        pBitsRect->rect = *pRect;
    }

    pBitsRect->cBitsPerPixel = pScreen->sb.pixelBuffer.bytesPerPixel * 8;
    pBitsRect->cbPixel       = pScreen->sb.pixelBuffer.bytesPerPixel;
    pBitsRect->cbLine        = pScreen->sb.pixelBuffer.lineSize;

    SBLOG(("src %p, %d, %d, %d, %d\n", pScreen->sb.src, pBitsRect->rect.y, pBitsRect->cbLine, pBitsRect->rect.x, pBitsRect->cbPixel));

    pBitsRect->pu8Bits = pScreen->sb.pixelBuffer.pu8Pixels +
                         pBitsRect->rect.y * pBitsRect->cbLine +
                         pBitsRect->rect.x * pBitsRect->cbPixel;

    SBLOG(("bits %p\n", pBitsRect->pu8Bits));

    return pScreen->u32AccessKey;
}

void shadowBufferUnlock (unsigned uScreenId)
{
#ifdef DEBUG
    VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);
    if (pScreen != NULL)
    {
        pScreen->sb.cAccessed--;
        Assert(pScreen->sb.cAccessed >= 0);
    }
#else
    NOREF(uScreenId);
#endif /* DEBUG */

    sbUnlock ();
}

void shadowBufferSetAccessible(unsigned uScreenId, bool fAccessible)
{
    if (sbLock (~0U))
    {
        VRDPSBSCREEN *pScreen = sbResolveScreenId (uScreenId);

        if (pScreen != NULL)
        {
            pScreen->fAccessible = fAccessible;
        }

        sbUnlock ();
    }
}
