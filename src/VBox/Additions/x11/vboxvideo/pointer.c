/* $Id: pointer.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox X11 Additions graphics driver utility functions
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef PCIACCESS
# include "xf86Pci.h"
# include <Pci.h>
#endif

#include "xf86.h"
#define NEED_XF86_TYPES
#include "compiler.h"
#include "cursorstr.h"
#include "servermd.h"

#include "vboxvideo.h"

#ifdef XORG_7X
# include <stdlib.h>
# include <string.h>
#endif

#define VBOX_MAX_CURSOR_WIDTH 64
#define VBOX_MAX_CURSOR_HEIGHT 64

/**************************************************************************
* Debugging functions and macros                                          *
**************************************************************************/

/* #define DEBUG_POINTER */

#ifdef DEBUG
# define PUT_PIXEL(c) ErrorF ("%c", c)
#else /* DEBUG_VIDEO not defined */
# define PUT_PIXEL(c) do { } while(0)
#endif /* DEBUG_VIDEO not defined */

/** Macro to printf an error message and return from a function */
#define RETERROR(scrnIndex, RetVal, ...) \
    do \
    { \
        xf86DrvMsg(scrnIndex, X_ERROR, __VA_ARGS__); \
        return RetVal; \
    } \
    while (0)

/** Structure to pass cursor image data between realise_cursor() and
 * load_cursor_image().  The members match the parameters to
 * @a VBoxHGSMIUpdatePointerShape(). */
struct vboxCursorImage
{
    uint32_t fFlags;
    uint32_t cHotX;
    uint32_t cHotY;
    uint32_t cWidth;
    uint32_t cHeight;
    uint8_t *pPixels;
    uint32_t cbLength;
};

#ifdef DEBUG_POINTER
static void
vbox_show_shape(unsigned short w, unsigned short h, CARD32 bg, unsigned char *image)
{
    size_t x, y;
    unsigned short pitch;
    CARD32 *color;
    unsigned char *mask;
    size_t sizeMask;

    image    += sizeof(struct vboxCursorImage);
    mask      = image;
    pitch     = (w + 7) / 8;
    sizeMask  = (pitch * h + 3) & ~3;
    color     = (CARD32 *)(image + sizeMask);

    TRACE_ENTRY();
    for (y = 0; y < h; ++y, mask += pitch, color += w)
    {
        for (x = 0; x < w; ++x)
        {
            if (mask[x / 8] & (1 << (7 - (x % 8))))
                ErrorF (" ");
            else
            {
                CARD32 c = color[x];
                if (c == bg)
                    ErrorF("Y");
                else
                    ErrorF("X");
            }
        }
        ErrorF("\n");
    }
}
#endif

/**************************************************************************
* Main functions                                                          *
**************************************************************************/

void vbvxCursorTerm(VBOXPtr pVBox)
{
    TRACE_ENTRY();

    xf86DestroyCursorInfoRec(pVBox->pCurs);
    pVBox->pCurs = NULL;
    TRACE_EXIT();
}

static void
vbox_vmm_hide_cursor(ScrnInfoPtr pScrn, VBOXPtr pVBox)
{
    int rc;
    RT_NOREF(pScrn);

    rc = VBoxHGSMIUpdatePointerShape(&pVBox->guestCtx, 0, 0, 0, 0, 0, NULL, 0);
    AssertMsg(rc == VINF_SUCCESS, ("Could not hide the virtual mouse pointer, VBox error %d.\n", rc));
}

static void
vbox_vmm_show_cursor(ScrnInfoPtr pScrn, VBOXPtr pVBox)
{
    int rc;
    RT_NOREF(pScrn);

    if (!pVBox->fUseHardwareCursor)
        return;
    rc = VBoxHGSMIUpdatePointerShape(&pVBox->guestCtx, VBOX_MOUSE_POINTER_VISIBLE,
                                     0, 0, 0, 0, NULL, 0);
    AssertMsg(rc == VINF_SUCCESS, ("Could not unhide the virtual mouse pointer.\n"));
}

static void
vbox_vmm_load_cursor_image(ScrnInfoPtr pScrn, VBOXPtr pVBox,
                           unsigned char *pvImage)
{
    int rc;
    struct vboxCursorImage *pImage;
    pImage = (struct vboxCursorImage *)pvImage;
    RT_NOREF(pScrn);

#ifdef DEBUG_POINTER
    vbox_show_shape(pImage->cWidth, pImage->cHeight, 0, pvImage);
#endif

    rc = VBoxHGSMIUpdatePointerShape(&pVBox->guestCtx, pImage->fFlags,
             pImage->cHotX, pImage->cHotY, pImage->cWidth, pImage->cHeight,
             pImage->pPixels, pImage->cbLength);
    AssertMsg(rc == VINF_SUCCESS, ("Unable to set the virtual mouse pointer image.\n"));
}

static void
vbox_set_cursor_colors(ScrnInfoPtr pScrn, int bg, int fg)
{
    RT_NOREF(pScrn);
    RT_NOREF(bg);
    RT_NOREF(fg);
    /* ErrorF("vbox_set_cursor_colors NOT IMPLEMENTED\n"); */
}


static void
vbox_set_cursor_position(ScrnInfoPtr pScrn, int x, int y)
{
    VBOXPtr pVBox = pScrn->driverPrivate;

    VBoxHGSMIReportCursorPosition(&pVBox->guestCtx, true, x, y, NULL, NULL);
}

static void
vbox_hide_cursor(ScrnInfoPtr pScrn)
{
    VBOXPtr pVBox = pScrn->driverPrivate;

    vbox_vmm_hide_cursor(pScrn, pVBox);
}

static void
vbox_show_cursor(ScrnInfoPtr pScrn)
{
    VBOXPtr pVBox = pScrn->driverPrivate;

    vbox_vmm_show_cursor(pScrn, pVBox);
}

static void
vbox_load_cursor_image(ScrnInfoPtr pScrn, unsigned char *image)
{
    VBOXPtr pVBox = pScrn->driverPrivate;

    vbox_vmm_load_cursor_image(pScrn, pVBox, image);
}

static Bool
vbox_use_hw_cursor(ScreenPtr pScreen, CursorPtr pCurs)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VBOXPtr pVBox = pScrn->driverPrivate;
    RT_NOREF(pCurs);
    return pVBox->fUseHardwareCursor;
}

static unsigned char
color_to_byte(unsigned c)
{
    return (c >> 8) & 0xff;
}

static unsigned char *
vbox_realize_cursor(xf86CursorInfoPtr infoPtr, CursorPtr pCurs)
{
    VBOXPtr pVBox;
    CursorBitsPtr bitsp;
    unsigned short w, h, x, y;
    unsigned char *c, *p, *pm, *ps, *m;
    size_t sizeRequest, sizeRgba, sizeMask, srcPitch, dstPitch;
    CARD32 fc, bc, *cp;
    int scrnIndex = infoPtr->pScrn->scrnIndex;
    struct vboxCursorImage *pImage;

    pVBox = infoPtr->pScrn->driverPrivate;
    bitsp = pCurs->bits;
    w = bitsp->width;
    h = bitsp->height;

    if (!w || !h || w > VBOX_MAX_CURSOR_WIDTH || h > VBOX_MAX_CURSOR_HEIGHT)
        RETERROR(scrnIndex, NULL,
            "Error invalid cursor dimensions %dx%d\n", w, h);

    if ((bitsp->xhot > w) || (bitsp->yhot > h))
        RETERROR(scrnIndex, NULL,
            "Error invalid cursor hotspot location %dx%d (max %dx%d)\n",
            bitsp->xhot, bitsp->yhot, w, h);

    srcPitch = PixmapBytePad (bitsp->width, 1);
    dstPitch = (w + 7) / 8;
    sizeMask = ((dstPitch * h) + 3) & (size_t) ~3;
    sizeRgba = w * h * 4;
    sizeRequest = sizeMask + sizeRgba + sizeof(*pImage);

    p = c = calloc (1, sizeRequest);
    if (!c)
        RETERROR(scrnIndex, NULL,
                 "Error failed to alloc %lu bytes for cursor\n",
                 (unsigned long) sizeRequest);

    pImage = (struct vboxCursorImage *)p;
    pImage->pPixels = m = p + sizeof(*pImage);
    cp = (CARD32 *)(m + sizeMask);

    TRACE_LOG ("w=%d h=%d sm=%d sr=%d p=%d\n",
           w, h, (int) sizeMask, (int) sizeRgba, (int) dstPitch);
    TRACE_LOG ("m=%p c=%p cp=%p\n", m, c, (void *)cp);

    fc = color_to_byte (pCurs->foreBlue)
      | (color_to_byte (pCurs->foreGreen) << 8)
      | (color_to_byte (pCurs->foreRed)   << 16);

    bc = color_to_byte (pCurs->backBlue)
      | (color_to_byte (pCurs->backGreen) << 8)
      | (color_to_byte (pCurs->backRed)   << 16);

    /*
     * Convert the Xorg source/mask bits to the and/xor bits VBox needs.
     * Xorg:
     *   The mask is a bitmap indicating which parts of the cursor are
     *   transparent and which parts are drawn. The source is a bitmap
     *   indicating which parts of the non-transparent portion of the
     *   the cursor should be painted in the foreground color and which
     *   should be painted in the background color. By default, set bits
     *   indicate the opaque part of the mask bitmap and clear bits
     *   indicate the transparent part.
     * VBox:
     *   The color data is the XOR mask. The AND mask bits determine
     *   which pixels of the color data (XOR mask) will replace (overwrite)
     *   the screen pixels (AND mask bit = 0) and which ones will be XORed
     *   with existing screen pixels (AND mask bit = 1).
     *   For example when you have the AND mask all 0, then you see the
     *   correct mouse pointer image surrounded by black square.
     */
    for (pm = bitsp->mask, ps = bitsp->source, y = 0;
         y < h;
         ++y, pm += srcPitch, ps += srcPitch, m += dstPitch)
    {
        for (x = 0; x < w; ++x)
        {
            if (pm[x / 8] & (1 << (x % 8)))
            {
                /* opaque, leave AND mask bit at 0 */
                if (ps[x / 8] & (1 << (x % 8)))
                {
                    *cp++ = fc;
                    PUT_PIXEL('X');
                }
                else
                {
                    *cp++ = bc;
                    PUT_PIXEL('*');
                }
            }
            else
            {
                /* transparent, set AND mask bit */
                m[x / 8] |= 1 << (7 - (x % 8));
                /* don't change the screen pixel */
                *cp++ = 0;
                PUT_PIXEL(' ');
            }
        }
        PUT_PIXEL('\n');
    }

    pImage->cWidth   = w;
    pImage->cHeight  = h;
    pImage->cHotX    = bitsp->xhot;
    pImage->cHotY    = bitsp->yhot;
    pImage->fFlags   = VBOX_MOUSE_POINTER_VISIBLE | VBOX_MOUSE_POINTER_SHAPE;
    pImage->cbLength = sizeRequest - sizeof(*pImage);

#ifdef DEBUG_POINTER
    ErrorF("shape = %p\n", p);
    vbox_show_shape(w, h, bc, c);
#endif

    return p;
}

#ifdef ARGB_CURSOR
static Bool
vbox_use_hw_cursor_argb(ScreenPtr pScreen, CursorPtr pCurs)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VBOXPtr pVBox = pScrn->driverPrivate;

    if (!pVBox->fUseHardwareCursor)
        return FALSE;
    if (   (pCurs->bits->height > VBOX_MAX_CURSOR_HEIGHT)
        || (pCurs->bits->width > VBOX_MAX_CURSOR_WIDTH)
        || (pScrn->bitsPerPixel <= 8))
        return FALSE;
    return TRUE;
}


static void
vbox_load_cursor_argb(ScrnInfoPtr pScrn, CursorPtr pCurs)
{
    VBOXPtr pVBox;
    CursorBitsPtr bitsp;
    unsigned short w, h;
    unsigned short cx, cy;
    unsigned char *pm;
    CARD32 *pc;
    size_t sizeData, sizeMask;
    CARD8 *p;
    int scrnIndex;
    uint32_t fFlags =   VBOX_MOUSE_POINTER_VISIBLE | VBOX_MOUSE_POINTER_SHAPE
                      | VBOX_MOUSE_POINTER_ALPHA;

    pVBox = pScrn->driverPrivate;
    bitsp = pCurs->bits;
    w     = bitsp->width;
    h     = bitsp->height;
    scrnIndex = pScrn->scrnIndex;

    /* Mask must be generated for alpha cursors, that is required by VBox. */
    /* note: (michael) the next struct must be 32bit aligned. */
    sizeMask  = ((w + 7) / 8 * h + 3) & ~3;

    if (!w || !h || w > VBOX_MAX_CURSOR_WIDTH || h > VBOX_MAX_CURSOR_HEIGHT)
        RETERROR(scrnIndex, ,
                 "Error invalid cursor dimensions %dx%d\n", w, h);

    if ((bitsp->xhot > w) || (bitsp->yhot > h))
        RETERROR(scrnIndex, ,
                 "Error invalid cursor hotspot location %dx%d (max %dx%d)\n",
                 bitsp->xhot, bitsp->yhot, w, h);

    sizeData = w * h * 4 + sizeMask;
    p = calloc(1, sizeData);
    if (!p)
        RETERROR(scrnIndex, ,
                 "Error failed to alloc %lu bytes for cursor\n",
                 (unsigned long)sizeData);

    memcpy(p + sizeMask, bitsp->argb, w * h * 4);

    /* Emulate the AND mask. */
    pm = p;
    pc = bitsp->argb;

    /* Init AND mask to 1 */
    memset(pm, 0xFF, sizeMask);

    /*
     * The additions driver must provide the AND mask for alpha cursors. The host frontend
     * which can handle alpha channel, will ignore the AND mask and draw an alpha cursor.
     * But if the host does not support ARGB, then it simply uses the AND mask and the color
     * data to draw a normal color cursor.
     */
    for (cy = 0; cy < h; cy++)
    {
        unsigned char bitmask = 0x80;

        for (cx = 0; cx < w; cx++, bitmask >>= 1)
        {
            if (bitmask == 0)
                bitmask = 0x80;

            if (pc[cx] >= 0xF0000000)
                pm[cx / 8] &= ~bitmask;
        }

        /* Point to next source and dest scans */
        pc += w;
        pm += (w + 7) / 8;
    }

    VBoxHGSMIUpdatePointerShape(&pVBox->guestCtx, fFlags, bitsp->xhot,
                                bitsp->yhot, w, h, p, sizeData);
    free(p);
}
#endif

Bool vbvxCursorInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VBOXPtr pVBox = pScrn->driverPrivate;
    xf86CursorInfoPtr pCurs = NULL;
    Bool rc = TRUE;

    TRACE_ENTRY();
    pVBox->pCurs = pCurs = xf86CreateCursorInfoRec();
    if (!pCurs) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to create X Window cursor information structures for virtual mouse.\n");
        rc = FALSE;
    }
    if (rc) {
        pCurs->MaxWidth = VBOX_MAX_CURSOR_WIDTH;
        pCurs->MaxHeight = VBOX_MAX_CURSOR_HEIGHT;
        pCurs->Flags =   HARDWARE_CURSOR_TRUECOLOR_AT_8BPP
                       | HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1
                       | HARDWARE_CURSOR_BIT_ORDER_MSBFIRST
                       | HARDWARE_CURSOR_UPDATE_UNHIDDEN;

        pCurs->SetCursorColors   = vbox_set_cursor_colors;
        pCurs->SetCursorPosition = vbox_set_cursor_position;
        pCurs->LoadCursorImage   = vbox_load_cursor_image;
        pCurs->HideCursor        = vbox_hide_cursor;
        pCurs->ShowCursor        = vbox_show_cursor;
        pCurs->UseHWCursor       = vbox_use_hw_cursor;
        pCurs->RealizeCursor     = vbox_realize_cursor;

#ifdef ARGB_CURSOR
        pCurs->UseHWCursorARGB   = vbox_use_hw_cursor_argb;
        pCurs->LoadCursorARGB    = vbox_load_cursor_argb;
#endif

        rc = xf86InitCursor(pScreen, pCurs);
    }
    if (!rc)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to enable mouse pointer integration.\n");
    if (!rc && (pCurs != NULL))
        xf86DestroyCursorInfoRec(pCurs);
    return rc;
}
