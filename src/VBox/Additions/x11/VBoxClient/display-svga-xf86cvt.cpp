/* $Id: display-svga-xf86cvt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Our version of xf86CVTMode.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 * This file is based on x.org server 1.18.0 file xf86cvt.c:
 *
 * Copyright 2005-2006 Luc Verhaegen.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#if 0
/*
 * The reason for having this function in a file of its own is
 * so that ../utils/cvt/cvt can link to it, and that xf86CVTMode
 * code is shared directly.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#else
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#endif

#include "xf86.h"
#include "xf86Modes.h"

#include <string.h>
#else
# include "VBoxClient.h"
# include "display-svga-xf86cvt.h"
#endif

/*
 * This is a slightly modified version of the xf86CVTMode function from
 * xf86cvt.c from the xorg xserver source code.  Computes several parameters
 * of a display mode out of horizontal and vertical resolutions.  Replicated
 * here to avoid further dependencies.
 *
 *----------------------------------------------------------------------------
 *
 * Generate a CVT standard mode from HDisplay, VDisplay and VRefresh.
 *
 * These calculations are stolen from the CVT calculation spreadsheet written
 * by Graham Loveridge. He seems to be claiming no copyright and there seems to
 * be no license attached to this. He apparently just wants to see his name
 * mentioned.
 *
 * This file can be found at http://www.vesa.org/Public/CVT/CVTd6r1.xls
 *
 * Comments and structure corresponds to the comments and structure of the xls.
 * This should ease importing of future changes to the standard (not very
 * likely though).
 *
 * About margins; i'm sure that they are to be the bit between HDisplay and
 * HBlankStart, HBlankEnd and HTotal, VDisplay and VBlankStart, VBlankEnd and
 * VTotal, where the overscan colour is shown. FB seems to call _all_ blanking
 * outside sync "margin" for some reason. Since we prefer seeing proper
 * blanking instead of the overscan colour, and since the Crtc* values will
 * probably get altered after us, we will disable margins altogether. With
 * these calculations, Margins will plainly expand H/VDisplay, and we don't
 * want that. -- libv
 *
 */
DisplayModeR VBoxClient_xf86CVTMode(int HDisplay, int VDisplay, float VRefresh /* Herz */, bool Reduced, bool Interlaced)
{
    DisplayModeR Mode;

    /* 1) top/bottom margin size (% of height) - default: 1.8 */
#define CVT_MARGIN_PERCENTAGE 1.8

    /* 2) character cell horizontal granularity (pixels) - default 8 */
#define CVT_H_GRANULARITY 8

    /* 4) Minimum vertical porch (lines) - default 3 */
#define CVT_MIN_V_PORCH 3

    /* 4) Minimum number of vertical back porch lines - default 6 */
#define CVT_MIN_V_BPORCH 6

    /* Pixel Clock step (kHz) */
#define CVT_CLOCK_STEP 250

    bool Margins = false;
    float VFieldRate, HPeriod;
    int HDisplayRnd, HMargin;
    int VDisplayRnd, VMargin, VSync;
    float Interlace;            /* Please rename this */

    /* CVT default is 60.0Hz */
    if (!VRefresh)
        VRefresh = 60.0;

    /* 1. Required field rate */
    if (Interlaced)
        VFieldRate = VRefresh * 2;
    else
        VFieldRate = VRefresh;

    /* 2. Horizontal pixels */
    HDisplayRnd = HDisplay - (HDisplay % CVT_H_GRANULARITY);

    /* 3. Determine left and right borders */
    if (Margins) {
        /* right margin is actually exactly the same as left */
        HMargin = (int)((float)HDisplayRnd * CVT_MARGIN_PERCENTAGE / 100.0);
        HMargin -= HMargin % CVT_H_GRANULARITY;
    }
    else
        HMargin = 0;

    /* 4. Find total active pixels */
    Mode.HDisplay = HDisplayRnd + 2 * HMargin;

    /* 5. Find number of lines per field */
    if (Interlaced)
        VDisplayRnd = VDisplay / 2;
    else
        VDisplayRnd = VDisplay;

    /* 6. Find top and bottom margins */
    /* nope. */
    if (Margins)
        /* top and bottom margins are equal again. */
        VMargin = (int)((float)VDisplayRnd * CVT_MARGIN_PERCENTAGE / 100.0);
    else
        VMargin = 0;

    Mode.VDisplay = VDisplay + 2 * VMargin;

    /* 7. Interlace */
    if (Interlaced)
        Interlace = 0.5;
    else
        Interlace = 0.0;

    /* Determine VSync Width from aspect ratio */
    if (!(VDisplay % 3) && ((VDisplay * 4 / 3) == HDisplay))
        VSync = 4;
    else if (!(VDisplay % 9) && ((VDisplay * 16 / 9) == HDisplay))
        VSync = 5;
    else if (!(VDisplay % 10) && ((VDisplay * 16 / 10) == HDisplay))
        VSync = 6;
    else if (!(VDisplay % 4) && ((VDisplay * 5 / 4) == HDisplay))
        VSync = 7;
    else if (!(VDisplay % 9) && ((VDisplay * 15 / 9) == HDisplay))
        VSync = 7;
    else                        /* Custom */
        VSync = 10;

    if (!Reduced) {             /* simplified GTF calculation */

        /* 4) Minimum time of vertical sync + back porch interval (µs)
         * default 550.0 */
#define CVT_MIN_VSYNC_BP 550.0

        /* 3) Nominal HSync width (% of line period) - default 8 */
#define CVT_HSYNC_PERCENTAGE 8

        float HBlankPercentage;
        int VSyncAndBackPorch, VBackPorch;
        int HBlank;

        /* 8. Estimated Horizontal period */
        HPeriod = ((float)(1000000.0 / VFieldRate - CVT_MIN_VSYNC_BP))
                / (VDisplayRnd + 2 * VMargin + CVT_MIN_V_PORCH + Interlace);

        /* 9. Find number of lines in sync + backporch */
        if ((int)(CVT_MIN_VSYNC_BP / HPeriod) + 1 < VSync + CVT_MIN_V_PORCH)
            VSyncAndBackPorch = VSync + CVT_MIN_V_PORCH;
        else
            VSyncAndBackPorch = (int)(CVT_MIN_VSYNC_BP / HPeriod) + 1;

        /* 10. Find number of lines in back porch */
        VBackPorch = VSyncAndBackPorch - VSync;
        (void) VBackPorch;

        /* 11. Find total number of lines in vertical field */
        Mode.VTotal = (int)(VDisplayRnd + 2 * VMargin + VSyncAndBackPorch + Interlace + CVT_MIN_V_PORCH);

        /* 5) Definition of Horizontal blanking time limitation */
        /* Gradient (%/kHz) - default 600 */
#define CVT_M_FACTOR 600

        /* Offset (%) - default 40 */
#define CVT_C_FACTOR 40

        /* Blanking time scaling factor - default 128 */
#define CVT_K_FACTOR 128

        /* Scaling factor weighting - default 20 */
#define CVT_J_FACTOR 20

#define CVT_M_PRIME (CVT_M_FACTOR * CVT_K_FACTOR / 256)
#define CVT_C_PRIME ((CVT_C_FACTOR - CVT_J_FACTOR) * CVT_K_FACTOR / 256 + CVT_J_FACTOR)

        /* 12. Find ideal blanking duty cycle from formula */
        HBlankPercentage = CVT_C_PRIME - CVT_M_PRIME * HPeriod / 1000.0;

        /* 13. Blanking time */
        if (HBlankPercentage < 20)
            HBlankPercentage = 20;

        HBlank = (int)(Mode.HDisplay * HBlankPercentage / (100.0 - HBlankPercentage));
        HBlank -= HBlank % (2 * CVT_H_GRANULARITY);

        /* 14. Find total number of pixels in a line. */
        Mode.HTotal = Mode.HDisplay + HBlank;

        /* Fill in HSync values */
        Mode.HSyncEnd = Mode.HDisplay + HBlank / 2;

        Mode.HSyncStart = Mode.HSyncEnd - (Mode.HTotal * CVT_HSYNC_PERCENTAGE) / 100;
        Mode.HSyncStart += CVT_H_GRANULARITY - Mode.HSyncStart % CVT_H_GRANULARITY;

        /* Fill in VSync values */
        Mode.VSyncStart = Mode.VDisplay + CVT_MIN_V_PORCH;
        Mode.VSyncEnd = Mode.VSyncStart + VSync;

    }
    else {                      /* Reduced blanking */
        /* Minimum vertical blanking interval time (µs) - default 460 */
#define CVT_RB_MIN_VBLANK 460.0

        /* Fixed number of clocks for horizontal sync */
#define CVT_RB_H_SYNC 32.0

        /* Fixed number of clocks for horizontal blanking */
#define CVT_RB_H_BLANK 160.0

        /* Fixed number of lines for vertical front porch - default 3 */
#define CVT_RB_VFPORCH 3

        int VBILines;

        /* 8. Estimate Horizontal period. */
        HPeriod = ((float)(1000000.0 / VFieldRate - CVT_RB_MIN_VBLANK)) / (VDisplayRnd + 2 * VMargin);

        /* 9. Find number of lines in vertical blanking */
        VBILines = (int)((float)CVT_RB_MIN_VBLANK / HPeriod + 1);

        /* 10. Check if vertical blanking is sufficient */
        if (VBILines < CVT_RB_VFPORCH + VSync + CVT_MIN_V_BPORCH)
            VBILines = CVT_RB_VFPORCH + VSync + CVT_MIN_V_BPORCH;

        /* 11. Find total number of lines in vertical field */
        Mode.VTotal = (int)(VDisplayRnd + 2 * VMargin + Interlace + VBILines);

        /* 12. Find total number of pixels in a line */
        Mode.HTotal = (int)(Mode.HDisplay + CVT_RB_H_BLANK);

        /* Fill in HSync values */
        Mode.HSyncEnd = (int)(Mode.HDisplay + CVT_RB_H_BLANK / 2);
        Mode.HSyncStart = (int)(Mode.HSyncEnd - CVT_RB_H_SYNC);

        /* Fill in VSync values */
        Mode.VSyncStart = Mode.VDisplay + CVT_RB_VFPORCH;
        Mode.VSyncEnd = Mode.VSyncStart + VSync;
    }
    /* 15/13. Find pixel clock frequency (kHz for xf86) */
    Mode.Clock = (int)(Mode.HTotal * 1000.0 / HPeriod);
    Mode.Clock -= Mode.Clock % CVT_CLOCK_STEP;

    /* 16/14. Find actual Horizontal Frequency (kHz) */
    Mode.HSync = (float)Mode.Clock / (float)Mode.HTotal;

    /* 17/15. Find actual Field rate */
    Mode.VRefresh = (1000.0 * (float)Mode.Clock) / (float)(Mode.HTotal * Mode.VTotal);

    /* 18/16. Find actual vertical frame frequency */
    /* ignore - just set the mode flag for interlaced */
    if (Interlaced)
        Mode.VTotal *= 2;

#if 0
    XNFasprintf(&tmp, "%dx%d", HDisplay, VDisplay);
    Mode->name = tmp;

    if (Reduced)
        Mode->Flags |= V_PHSYNC | V_NVSYNC;
    else
        Mode->Flags |= V_NHSYNC | V_PVSYNC;

    if (Interlaced)
        Mode->Flags |= V_INTERLACE;
#endif

    return Mode;
}
