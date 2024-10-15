/* $Id: edid.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * Linux Additions X11 graphics driver, EDID construction
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 * This file is based on drmmode_display.c from the X.Org xf86-video-intel
 * driver with the following copyright notice:
 *
 * Copyright © 2007 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Dave Airlie <airlied@redhat.com>
 *    Michael Thayer <michael.thayer@oracle.com>
 */

#include "misc.h"
#include "xf86DDC.h"
#include "xf86Crtc.h"
#include "vboxvideo.h"

enum { EDID_SIZE = 128 };

const unsigned char g_acszEDIDBase[EDID_SIZE] =
{
   0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, /* header */
   0x58, 0x58, /* manufacturer (VBX) */
   0x00, 0x00, /* product code */
   0x00, 0x00,0x00, 0x00, /* serial number goes here */
   0x01, /* week of manufacture */
   0x00, /* year of manufacture */
   0x01, 0x03, /* EDID version */
   0x80, /* capabilities - digital */
   0x00, /* horiz. res in cm, zero for projectors */
   0x00, /* vert. res in cm */
   0x78, /* display gamma (120 == 2.2).  Should we ask the host for this? */
   0xEE, /* features (standby, suspend, off, RGB, standard colour space,
          * preferred timing mode) */
   0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54,
       /* chromaticity for standard colour space - should we ask the host? */
   0x00, 0x00, 0x00, /* no default timings */
   0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
   0x01, 0x01, 0x01, 0x01, /* no standard timings */
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* descriptor block 1 goes here */
   0x00, 0x00, 0x00, 0xFD, 0x00, /* descriptor block 2, monitor ranges */
   0x00, 0xC8, 0x00, 0xC8, 0x64, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20,
   0x20, /* 0-200Hz vertical, 0-200KHz horizontal, 1000MHz pixel clock */
   0x00, 0x00, 0x00, 0xFC, 0x00, /* descriptor block 3, monitor name */
   'V', 'B', 'O', 'X', ' ', 'm', 'o', 'n', 'i', 't', 'o', 'r', '\n',
   0x00, 0x00, 0x00, 0x10, 0x00, /* descriptor block 4: dummy data */
   0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
   0x20,
   0x00, /* number of extensions */
   0x00 /* checksum goes here */
};

static void fillDescBlockTimings(unsigned char *pchDescBlock,
                                 DisplayModePtr mode)
{
    struct detailed_timings timing;

    timing.clock = mode->Clock * 1000;
    timing.h_active = mode->HDisplay;
    timing.h_blanking = mode->HTotal - mode->HDisplay;
    timing.v_active = mode->VDisplay;
    timing.v_blanking = mode->VTotal - mode->VDisplay;
    timing.h_sync_off = mode->HSyncStart - mode->HDisplay;
    timing.h_sync_width = mode->HSyncEnd - mode->HSyncStart;
    timing.v_sync_off = mode->VSyncStart - mode->VDisplay;
    timing.v_sync_width = mode->VSyncEnd - mode->VSyncStart;
    pchDescBlock[0]   = (timing.clock / 10000) & 0xff;
    pchDescBlock[1]   = (timing.clock / 10000) >> 8;
    pchDescBlock[2]   = timing.h_active & 0xff;
    pchDescBlock[3]   = timing.h_blanking & 0xff;
    pchDescBlock[4]   = (timing.h_active >> 4) & 0xf0;
    pchDescBlock[4]  |= (timing.h_blanking >> 8) & 0xf;
    pchDescBlock[5]   = timing.v_active & 0xff;
    pchDescBlock[6]   = timing.v_blanking & 0xff;
    pchDescBlock[7]   = (timing.v_active >> 4) & 0xf0;
    pchDescBlock[7]  |= (timing.v_blanking >> 8) & 0xf;
    pchDescBlock[8]   = timing.h_sync_off & 0xff;
    pchDescBlock[9]   = timing.h_sync_width & 0xff;
    pchDescBlock[10]  = (timing.v_sync_off << 4) & 0xf0;
    pchDescBlock[10] |= timing.v_sync_width & 0xf;
    pchDescBlock[11]  = (timing.h_sync_off >> 2) & 0xC0;
    pchDescBlock[11] |= (timing.h_sync_width >> 4) & 0x30;
    pchDescBlock[11] |= (timing.v_sync_off >> 2) & 0xC;
    pchDescBlock[11] |= (timing.v_sync_width >> 4) & 0x3;
    pchDescBlock[12] = pchDescBlock[13] = pchDescBlock[14]
                     = pchDescBlock[15] = pchDescBlock[16]
                     = pchDescBlock[17] = 0;
}


static void setEDIDChecksum(unsigned char *pch)
{
    unsigned i, sum = 0;
    for (i = 0; i < EDID_SIZE - 1; ++i)
        sum += pch[i];
    pch[EDID_SIZE - 1] = (0x100 - (sum & 0xFF)) & 0xFF;
}


/**
 * Construct an EDID for an output given a preferred mode.  The main reason for
 * doing this is to confound gnome-settings-deamon which tries to reset the
 * last mode configuration if the same monitors are plugged in again, which is
 * a reasonable thing to do but not what we want in a VM.  We evily store
 * the (empty) raw EDID data at the end of the structure so that it gets
 * freed automatically along with the structure.
 */
Bool VBOXEDIDSet(xf86OutputPtr output, DisplayModePtr pmode)
{
    unsigned char *pch, *pchEDID;
    xf86MonPtr pEDIDMon;

    pch = calloc(1, sizeof(xf86Monitor) + EDID_SIZE);
    if (!pch)
    {
        xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
            "Can't allocate memory for EDID structure.\n");
        return FALSE;
    }
    pchEDID = pch + sizeof(xf86Monitor);
    memcpy(pchEDID, g_acszEDIDBase, EDID_SIZE);
    pchEDID[12] = pmode->HDisplay & 0xff;
    pchEDID[13] = pmode->HDisplay >> 8;
    pchEDID[14] = pmode->VDisplay & 0xff;
    pchEDID[15] = pmode->VDisplay >> 8;
    fillDescBlockTimings(pchEDID + 54, pmode);
    setEDIDChecksum(pchEDID);
    pEDIDMon = xf86InterpretEDID(output->scrn->scrnIndex, pchEDID);
    if (!pEDIDMon)
    {
        free(pch);
        return FALSE;
    }
    memcpy(pch, pEDIDMon, sizeof(xf86Monitor));
    free(pEDIDMon);
    pEDIDMon = (xf86MonPtr)pch;
    xf86OutputSetEDID(output, pEDIDMon);
    return TRUE;
}
