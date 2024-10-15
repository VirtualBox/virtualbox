/* $Id: setmode.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Linux Additions X11 graphics driver, mode setting
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 * This file is based on X11 VESA driver (hardly any traces left here):
 *
 * Copyright (c) 2000 by Conectiva S.A. (http://www.conectiva.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Conectiva Linux shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from
 * Conectiva Linux.
 *
 * Authors: Paulo César Pereira de Andrade <pcpa@conectiva.com.br>
 *          Michael Thayer <michael.thayer@oracle.com>
 */

#ifdef XORG_7X
/* We include <unistd.h> for Solaris below, and the ANSI C emulation layer
 * interferes with that. */
# define _XF86_ANSIC_H
# define XF86_LIBC_H
# include <string.h>
#endif
#include "vboxvideo.h"
#include "xf86.h"

/* VGA hardware functions for setting and restoring text mode */
#include "vgaHW.h"

#ifdef RT_OS_SOLARIS
# include <sys/vuid_event.h>
# include <sys/msio.h>
# include <errno.h>
# include <fcntl.h>
# include <unistd.h>
#endif

/** Clear the virtual framebuffer in VRAM.  Optionally also clear up to the
 * size of a new framebuffer.  Framebuffer sizes larger than available VRAM
 * be treated as zero and passed over. */
void vbvxClearVRAM(ScrnInfoPtr pScrn, size_t cbOldSize, size_t cbNewSize)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);

    /* Assume 32BPP - this is just a sanity test. */
    AssertMsg(   cbOldSize / 4 <= VBOX_VIDEO_MAX_VIRTUAL * VBOX_VIDEO_MAX_VIRTUAL
               && cbNewSize / 4 <= VBOX_VIDEO_MAX_VIRTUAL * VBOX_VIDEO_MAX_VIRTUAL,
               ("cbOldSize=%llu cbNewSize=%llu, max=%u.\n", (unsigned long long)cbOldSize, (unsigned long long)cbNewSize,
                VBOX_VIDEO_MAX_VIRTUAL * VBOX_VIDEO_MAX_VIRTUAL));
    if (cbOldSize > (size_t)pVBox->cbFBMax)
        cbOldSize = pVBox->cbFBMax;
    if (cbNewSize > (size_t)pVBox->cbFBMax)
        cbNewSize = pVBox->cbFBMax;
    memset(pVBox->base, 0, max(cbOldSize, cbNewSize));
}

/** Set a graphics mode.  Poke any required values into registers, do an HGSMI
 * mode set and tell the host we support advanced graphics functions.
 */
void vbvxSetMode(ScrnInfoPtr pScrn, unsigned cDisplay, unsigned cWidth, unsigned cHeight, int x, int y, Bool fEnabled,
                 Bool fConnected, struct vbvxFrameBuffer *pFrameBuffer)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    uint32_t offStart;
    uint16_t fFlags;
    int rc;
    Bool fEnabledAndVisible = fEnabled && x + cWidth <= pFrameBuffer->cWidth && y + cHeight <= pFrameBuffer->cHeight;
    /* Recent host code has a flag to blank the screen; older code needs BPP set to zero. */
    uint32_t cBPP = fEnabledAndVisible || pVBox->fHostHasScreenBlankingFlag ? pFrameBuffer->cBPP : 0;

    TRACE_LOG("cDisplay=%u, cWidth=%u, cHeight=%u, x=%d, y=%d, fEnabled=%d, fConnected=%d, pFrameBuffer: { x0=%d, y0=%d, cWidth=%u, cHeight=%u, cBPP=%u }\n",
              cDisplay, cWidth, cHeight, x, y, fEnabled, fConnected, pFrameBuffer->x0, pFrameBuffer->y0, pFrameBuffer->cWidth,
              pFrameBuffer->cHeight, pFrameBuffer->cBPP);
    AssertMsg(cWidth != 0 && cHeight != 0, ("cWidth = 0 or cHeight = 0\n"));
    offStart = (y * pFrameBuffer->cWidth + x) * pFrameBuffer->cBPP / 8;
    if (cDisplay == 0 && fEnabled)
        VBoxVideoSetModeRegisters(cWidth, cHeight, pFrameBuffer->cWidth, pFrameBuffer->cBPP, 0, x, y);
    fFlags = VBVA_SCREEN_F_ACTIVE;
    fFlags |= (fConnected ? 0 : VBVA_SCREEN_F_DISABLED);
    fFlags |= (!fEnabledAndVisible && pVBox->fHostHasScreenBlankingFlag ? VBVA_SCREEN_F_BLANK : 0);
    VBoxHGSMIProcessDisplayInfo(&pVBox->guestCtx, cDisplay, x - pFrameBuffer->x0, y - pFrameBuffer->y0, offStart,
                                pFrameBuffer->cWidth * pFrameBuffer->cBPP / 8, cWidth, cHeight, cBPP, fFlags);
    rc = VBoxHGSMIUpdateInputMapping(&pVBox->guestCtx, 0 - pFrameBuffer->x0, 0 - pFrameBuffer->y0, pFrameBuffer->cWidth,
                                     pFrameBuffer->cHeight);
    if (RT_FAILURE(rc))
        FatalError("Failed to update the input mapping.\n");
}

/** Tell the virtual mouse device about the new virtual desktop size. */
void vbvxSetSolarisMouseRange(int width, int height)
{
#ifdef RT_OS_SOLARIS
    int rc;
    int hMouse = open("/dev/mouse", O_RDWR);

    if (hMouse >= 0)
    {
        do {
            Ms_screen_resolution Res = { height, width };
            rc = ioctl(hMouse, MSIOSRESOLUTION, &Res);
        } while ((rc != 0) && (errno == EINTR));
        close(hMouse);
    }
#else
    (void)width; (void)height;
#endif
}
