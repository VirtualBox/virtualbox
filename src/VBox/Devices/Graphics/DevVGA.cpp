/* $Id: DevVGA.cpp 110351 2025-07-22 15:36:21Z michal.necasek@oracle.com $ */
/** @file
 * DevVGA - VBox VGA/VESA device.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * QEMU VGA Emulator.
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/* WARNING!!! All defines that affect VGAState should be placed in DevVGA.h !!!
 *            NEVER place them here as this would lead to VGASTATE inconsistency
 *            across different .cpp files !!!
 */

#ifdef VBOX_WITH_HGSMI
#define PCIDEV_2_VGASTATE(pPciDev)      ((PVGASTATE)((uintptr_t)pPciDev - RT_OFFSETOF(VGASTATE, Dev)))
#endif /* VBOX_WITH_HGSMI */

/* VGA text mode blinking constants (cursor and blinking chars). */
#define VGA_BLINK_PERIOD_FULL           (RT_NS_100MS * 4)   /**< Blink cycle length. */
#define VGA_BLINK_PERIOD_ON             (RT_NS_100MS * 2)   /**< How long cursor/text is visible. */

/* EGA compatible switch values (in high nibble).
 * XENIX 2.1.x/2.2.x is known to rely on the switch values.
 */
#define EGA_SWITCHES    0x90    /* Off-on-on-off, high-res color EGA display. */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_VGA
#include <iprt/asm-mem.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pgm.h>
#include <VBox/AssertGuest.h>
#ifdef IN_RING3
# include <iprt/mem.h>
# include <iprt/ctype.h>
#endif /* IN_RING3 */
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/file.h>
#include <iprt/time.h>
#include <iprt/string.h>
#include <iprt/uuid.h>

#include <iprt/formats/bmp.h>

#include <VBox/VMMDev.h>
#include <VBoxVideo.h>
#include <VBox/bioslogo.h>

/* should go BEFORE any other DevVGA include to make all DevVGA.h config defines be visible */
#include "DevVGA.h"

#if defined(IN_RING3) && !defined(VBOX_DEVICE_STRUCT_TESTCASE)
# include "DevVGAModes.h"
# include <stdio.h> /* sscan */
#endif

#include "VBoxDD.h"
#include "VBoxDD2.h"

#ifdef VBOX_WITH_VMSVGA
#include "DevVGA-SVGA.h"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** The BIOS boot menu text position, X. */
#define LOGO_F12TEXT_X       304
/** The BIOS boot menu text position, Y. */
#define LOGO_F12TEXT_Y       460

/** Width of the "Press F12 to select boot device." bitmap.
    Anything that exceeds the limit of F12BootText below is filled with
    background. */
#define LOGO_F12TEXT_WIDTH   286
/** Height of the boot device selection bitmap, see LOGO_F12TEXT_WIDTH. */
#define LOGO_F12TEXT_HEIGHT  12

/** The BIOS logo delay time (msec). */
#define LOGO_DELAY_TIME      2000

#define LOGO_MAX_WIDTH       640
#define LOGO_MAX_HEIGHT      480
#define LOGO_MAX_SIZE        LOGO_MAX_WIDTH * LOGO_MAX_HEIGHT * 4


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IN_RING3
/* "Press F12 to select boot device." bitmap. */
static const uint8_t g_abLogoF12BootText[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x07, 0x0F, 0x7C,
    0xF8, 0xF0, 0x01, 0xE0, 0x81, 0x9F, 0x3F, 0x00, 0x70, 0xF8, 0x00, 0xE0, 0xC3,
    0x07, 0x0F, 0x1F, 0x3E, 0x70, 0x00, 0xF0, 0xE1, 0xC3, 0x07, 0x0E, 0x00, 0x6E,
    0x7C, 0x60, 0xE0, 0xE1, 0xC3, 0x07, 0xC6, 0x80, 0x81, 0x31, 0x63, 0xC6, 0x00,
    0x30, 0x80, 0x61, 0x0C, 0x00, 0x36, 0x63, 0x00, 0x8C, 0x19, 0x83, 0x61, 0xCC,
    0x18, 0x36, 0x00, 0xCC, 0x8C, 0x19, 0xC3, 0x06, 0xC0, 0x8C, 0x31, 0x3C, 0x30,
    0x8C, 0x19, 0x83, 0x31, 0x60, 0x60, 0x00, 0x0C, 0x18, 0x00, 0x0C, 0x60, 0x18,
    0x00, 0x80, 0xC1, 0x18, 0x00, 0x30, 0x06, 0x60, 0x18, 0x30, 0x80, 0x01, 0x00,
    0x33, 0x63, 0xC6, 0x30, 0x00, 0x30, 0x63, 0x80, 0x19, 0x0C, 0x03, 0x06, 0x00,
    0x0C, 0x18, 0x18, 0xC0, 0x81, 0x03, 0x00, 0x03, 0x18, 0x0C, 0x00, 0x60, 0x30,
    0x06, 0x00, 0x87, 0x01, 0x18, 0x06, 0x0C, 0x60, 0x00, 0xC0, 0xCC, 0x98, 0x31,
    0x0C, 0x00, 0xCC, 0x18, 0x30, 0x0C, 0xC3, 0x80, 0x01, 0x00, 0x03, 0x66, 0xFE,
    0x18, 0x30, 0x00, 0xC0, 0x02, 0x06, 0x06, 0x00, 0x18, 0x8C, 0x01, 0x60, 0xE0,
    0x0F, 0x86, 0x3F, 0x03, 0x18, 0x00, 0x30, 0x33, 0x66, 0x0C, 0x03, 0x00, 0x33,
    0xFE, 0x0C, 0xC3, 0x30, 0xE0, 0x0F, 0xC0, 0x87, 0x9B, 0x31, 0x63, 0xC6, 0x00,
    0xF0, 0x80, 0x01, 0x03, 0x00, 0x06, 0x63, 0x00, 0x8C, 0x19, 0x83, 0x61, 0xCC,
    0x18, 0x06, 0x00, 0x6C, 0x8C, 0x19, 0xC3, 0x00, 0x80, 0x8D, 0x31, 0xC3, 0x30,
    0x8C, 0x19, 0x03, 0x30, 0xB3, 0xC3, 0x87, 0x0F, 0x1F, 0x00, 0x2C, 0x60, 0x80,
    0x01, 0xE0, 0x87, 0x0F, 0x00, 0x3E, 0x7C, 0x60, 0xF0, 0xE1, 0xE3, 0x07, 0x00,
    0x0F, 0x3E, 0x7C, 0xFC, 0x00, 0xC0, 0xC3, 0xC7, 0x30, 0x0E, 0x3E, 0x7C, 0x00,
    0xCC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x1E, 0xC0, 0x00, 0x60, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x60, 0x00, 0xC0, 0x00, 0x00, 0x00,
    0x0C, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xC0, 0x0C, 0x87, 0x31, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x06, 0x00, 0x00, 0x18, 0x00, 0x30, 0x00, 0x00, 0x00, 0x03, 0x00, 0x30,
    0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF8, 0x83, 0xC1, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x01, 0x00,
    0x00, 0x04, 0x00, 0x0E, 0x00, 0x00, 0x80, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
#endif /* IN_RING3 */

#ifndef VBOX_DEVICE_STRUCT_TESTCASE  /* Till the end of the file - doesn't count indent wise. */

#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable:4310 4245) /* Buggy warnings: cast truncates constant value; conversion from 'int' to 'const uint8_t', signed/unsigned mismatch */
#endif

/* force some bits to zero */
static const uint8_t sr_mask[8] = {
    (uint8_t)~0xfc,
    (uint8_t)~0xc2,
    (uint8_t)~0xf0,
    (uint8_t)~0xc0,
    (uint8_t)~0xf1,
    (uint8_t)~0xff,
    (uint8_t)~0xff,
    (uint8_t)~0x01,
};

static const uint8_t gr_mask[16] = {
    (uint8_t)~0xf0, /* 0x00 */
    (uint8_t)~0xf0, /* 0x01 */
    (uint8_t)~0xf0, /* 0x02 */
    (uint8_t)~0xe0, /* 0x03 */
    (uint8_t)~0xfc, /* 0x04 */
    (uint8_t)~0x84, /* 0x05 */
    (uint8_t)~0xf0, /* 0x06 */
    (uint8_t)~0xf0, /* 0x07 */
    (uint8_t)~0x00, /* 0x08 */
    (uint8_t)~0xff, /* 0x09 */
    (uint8_t)~0xff, /* 0x0a */
    (uint8_t)~0xff, /* 0x0b */
    (uint8_t)~0xff, /* 0x0c */
    (uint8_t)~0xff, /* 0x0d */
    (uint8_t)~0xff, /* 0x0e */
    (uint8_t)~0xff, /* 0x0f */
};

#ifdef _MSC_VER
# pragma warning(pop)
#endif

#define cbswap_32(__x) \
    ((uint32_t)((((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
                (((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
                (((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
                (((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) ))

#ifdef WORDS_BIGENDIAN
# define PAT(x) cbswap_32(x)
#else
# define PAT(x) (x)
#endif

#ifdef WORDS_BIGENDIAN
# define BIG 1
#else
# define BIG 0
#endif

#ifdef WORDS_BIGENDIAN
#define GET_PLANE(data, p) (((data) >> (24 - (p) * 8)) & 0xff)
#else
#define GET_PLANE(data, p) (((data) >> ((p) * 8)) & 0xff)
#endif

static const uint32_t mask16[16] = {
    PAT(0x00000000),
    PAT(0x000000ff),
    PAT(0x0000ff00),
    PAT(0x0000ffff),
    PAT(0x00ff0000),
    PAT(0x00ff00ff),
    PAT(0x00ffff00),
    PAT(0x00ffffff),
    PAT(0xff000000),
    PAT(0xff0000ff),
    PAT(0xff00ff00),
    PAT(0xff00ffff),
    PAT(0xffff0000),
    PAT(0xffff00ff),
    PAT(0xffffff00),
    PAT(0xffffffff),
};

#undef PAT

#ifdef WORDS_BIGENDIAN
# define PAT(x) (x)
#else
# define PAT(x) cbswap_32(x)
#endif

#ifdef IN_RING3

static const uint32_t dmask16[16] = {
    PAT(0x00000000),
    PAT(0x000000ff),
    PAT(0x0000ff00),
    PAT(0x0000ffff),
    PAT(0x00ff0000),
    PAT(0x00ff00ff),
    PAT(0x00ffff00),
    PAT(0x00ffffff),
    PAT(0xff000000),
    PAT(0xff0000ff),
    PAT(0xff00ff00),
    PAT(0xff00ffff),
    PAT(0xffff0000),
    PAT(0xffff00ff),
    PAT(0xffffff00),
    PAT(0xffffffff),
};

static const uint32_t dmask4[4] = {
    PAT(0x00000000),
    PAT(0x0000ffff),
    PAT(0xffff0000),
    PAT(0xffffffff),
};

static uint32_t expand4[256];
static uint16_t expand2[256];
static uint8_t expand4to8[16];

#endif /* IN_RING3 */


/**
 * Mark a page in VGA A0000-BFFFF range as remapped.
 *
 * @param   pThis       VGA instance data.
 * @param   offVGAMem   The offset within VGA memory.
 */
DECLINLINE(void) vgaMarkRemapped(PVGASTATE pThis, RTGCPHYS offVGAMem)
{
    AssertMsg(offVGAMem < _128K, ("offVGAMem = %RGp\n", offVGAMem));
    pThis->bmPageRemappedVGA |= RT_BIT_32((uint32_t)offVGAMem >> GUEST_PAGE_SHIFT);
}


/**
 * Checks if a page in VGA A0000-BFFFF range is remapped.
 *
 * @returns true if remapped.
 * @returns false if not remapped (accesses will trap).
 * @param   pThis       VGA instance data.
 * @param   offVGAMem   The offset within VGA memory.
 */
DECLINLINE(bool) vgaIsRemapped(PVGASTATE pThis, RTGCPHYS offVGAMem)
{
    AssertMsg(offVGAMem < _128K, ("offVGAMem = %RGp\n", offVGAMem));
    return pThis->bmPageRemappedVGA & RT_BIT_32((uint32_t)offVGAMem >> GUEST_PAGE_SHIFT);
}


/**
 * Reset page remap tracking bits.
 *
 * @param   pThis           VGA instance data.
 */
DECLINLINE(void) vgaResetRemapped(PVGASTATE pThis)
{
    pThis->bmPageRemappedVGA = 0;
}


/**
 * Set a VRAM page dirty.
 *
 * @param   pThis       VGA instance data.
 * @param   offVRAM     The VRAM offset of the page to set.
 */
DECLINLINE(void) vgaR3MarkDirty(PVGASTATE pThis, RTGCPHYS offVRAM)
{
    AssertMsg(offVRAM < pThis->vram_size, ("offVRAM = %p, pThis->vram_size = %p\n", offVRAM, pThis->vram_size));
    ASMBitSet(&pThis->bmDirtyBitmap[0], offVRAM >> GUEST_PAGE_SHIFT);
    pThis->fHasDirtyBits = true;
}

#ifdef IN_RING3

/**
 * Tests if a VRAM page is dirty.
 *
 * @returns true if dirty.
 * @returns false if clean.
 * @param   pThis       VGA instance data.
 * @param   offVRAM     The VRAM offset of the page to check.
 */
DECLINLINE(bool) vgaR3IsDirty(PVGASTATE pThis, RTGCPHYS offVRAM)
{
    AssertMsg(offVRAM < pThis->vram_size, ("offVRAM = %p, pThis->vram_size = %p\n", offVRAM, pThis->vram_size));
    return ASMBitTest(&pThis->bmDirtyBitmap[0], offVRAM >> GUEST_PAGE_SHIFT);
}


/**
 * Reset dirty flags in a give range.
 *
 * @param   pThis           VGA instance data.
 * @param   offVRAMStart    Offset into the VRAM buffer of the first page.
 * @param   offVRAMEnd      Offset into the VRAM buffer of the last page - exclusive.
 */
DECLINLINE(void) vgaR3ResetDirty(PVGASTATE pThis, RTGCPHYS offVRAMStart, RTGCPHYS offVRAMEnd)
{
    AssertReturnVoid(offVRAMStart < pThis->vram_size);
    AssertReturnVoid(offVRAMEnd <= pThis->vram_size);
    AssertReturnVoid(offVRAMStart < offVRAMEnd);
    ASMBitClearRange(&pThis->bmDirtyBitmap[0], offVRAMStart >> GUEST_PAGE_SHIFT, offVRAMEnd >> GUEST_PAGE_SHIFT);
}


/**
 * Queries the VRAM dirty bits and resets the monitoring.
 */
static void vgaR3UpdateDirtyBitsAndResetMonitoring(PPDMDEVINS pDevIns, PVGASTATE pThis)
{
    size_t const cbBitmap = RT_ALIGN_Z(RT_MIN(pThis->vram_size, VGA_VRAM_MAX), GUEST_PAGE_SIZE * 64) / GUEST_PAGE_SIZE / 8;

    /*
     * If we don't have any dirty bits from MMIO accesses, we can just query
     * straight into the dirty buffer.
     */
    if (!pThis->fHasDirtyBits)
    {
        int rc = PDMDevHlpMmio2QueryAndResetDirtyBitmap(pDevIns, pThis->hMmio2VRam, pThis->bmDirtyBitmap, cbBitmap);
        AssertRC(rc);
    }
    /*
     * Otherwise we'll have to query and merge the two.
     */
    else
    {
        uint64_t bmDirtyPages[VGA_VRAM_MAX / GUEST_PAGE_SIZE / 64]; /* (256 MB VRAM -> 8KB bitmap) */
        int rc = PDMDevHlpMmio2QueryAndResetDirtyBitmap(pDevIns, pThis->hMmio2VRam, bmDirtyPages, cbBitmap);
        if (RT_SUCCESS(rc))
        {
            /** @todo could use ORPS or VORPS here, I think. */
            uint64_t     *pbmDst      = pThis->bmDirtyBitmap;
            size_t const  cTodo       = cbBitmap / sizeof(uint64_t);

            /* Do 64 bytes at a time first. */
            size_t const  cTodoFirst  = cTodo & ~(size_t)7;
            size_t        idx;
            for (idx = 0; idx < cTodoFirst; idx += 8)
            {
                pbmDst[idx + 0] |= bmDirtyPages[idx + 0];
                pbmDst[idx + 1] |= bmDirtyPages[idx + 1];
                pbmDst[idx + 2] |= bmDirtyPages[idx + 2];
                pbmDst[idx + 3] |= bmDirtyPages[idx + 3];
                pbmDst[idx + 4] |= bmDirtyPages[idx + 4];
                pbmDst[idx + 5] |= bmDirtyPages[idx + 5];
                pbmDst[idx + 6] |= bmDirtyPages[idx + 6];
                pbmDst[idx + 7] |= bmDirtyPages[idx + 7];
            }

            /* Then do a mopup of anything remaining. */
            switch (cTodo - idx)
            {
                case 7:     pbmDst[idx + 6] |= bmDirtyPages[idx + 6]; RT_FALL_THRU();
                case 6:     pbmDst[idx + 5] |= bmDirtyPages[idx + 5]; RT_FALL_THRU();
                case 5:     pbmDst[idx + 4] |= bmDirtyPages[idx + 4]; RT_FALL_THRU();
                case 4:     pbmDst[idx + 3] |= bmDirtyPages[idx + 3]; RT_FALL_THRU();
                case 3:     pbmDst[idx + 2] |= bmDirtyPages[idx + 2]; RT_FALL_THRU();
                case 2:     pbmDst[idx + 1] |= bmDirtyPages[idx + 1]; RT_FALL_THRU();
                case 1:     pbmDst[idx]     |= bmDirtyPages[idx];     break;
                case 0:     break;
                default:    AssertFailedBreak();
            }

            pThis->fHasDirtyBits = false;
        }
    }
}

#endif /* IN_RING3 */

/* Update the values needed for calculating Vertical Retrace and
 * Display Enable status bits more or less accurately. The Display Enable
 * bit is set (indicating *disabled* display signal) when either the
 * horizontal (hblank) or vertical (vblank) blanking is active. The
 * Vertical Retrace bit is set when vertical retrace (vsync) is active.
 * Unless the CRTC is horribly misprogrammed, vsync implies vblank.
 */
static void vga_update_retrace_state(PVGASTATE pThis)
{
    unsigned        htotal_cclks, vtotal_lines, chars_per_sec;
    unsigned        hblank_start_cclk, hblank_end_cclk, hblank_width, hblank_skew_cclks;
    unsigned        vsync_start_line, vsync_end, vsync_width;
    unsigned        vblank_start_line, vblank_end, vblank_width;
    unsigned        char_dots, clock_doubled, clock_index;
    const int       clocks[] = {25175000, 28322000, 25175000, 25175000};
    vga_retrace_s   *r = &pThis->retrace_state;

    /* For horizontal timings, we only care about the blanking start/end. */
    htotal_cclks = pThis->cr[0x00] + 5;
    hblank_start_cclk = pThis->cr[0x02];
    hblank_end_cclk = (pThis->cr[0x03] & 0x1f) + ((pThis->cr[0x05] & 0x80) >> 2);
    hblank_skew_cclks = (pThis->cr[0x03] >> 5) & 3;

    /* For vertical timings, we need both the blanking start/end... */
    vtotal_lines = pThis->cr[0x06] + ((pThis->cr[0x07] & 1) << 8) + ((pThis->cr[0x07] & 0x20) << 4) + 2;
    vblank_start_line = pThis->cr[0x15] + ((pThis->cr[0x07] & 8) << 5) + ((pThis->cr[0x09] & 0x20) << 4);
    vblank_end = pThis->cr[0x16];
    /* ... and the vertical retrace (vsync) start/end. */
    vsync_start_line = pThis->cr[0x10] + ((pThis->cr[0x07] & 4) << 6) + ((pThis->cr[0x07] & 0x80) << 2);
    vsync_end = pThis->cr[0x11] & 0xf;

    /* Calculate the blanking and sync widths. The way it's implemented in
     * the VGA with limited-width compare counters is quite a piece of work.
     */
    hblank_width = (hblank_end_cclk - hblank_start_cclk) & 0x3f;/* 6 bits */
    vblank_width = (vblank_end - vblank_start_line) & 0xff;     /* 8 bits */
    vsync_width  = (vsync_end - vsync_start_line) & 0xf;        /* 4 bits */

    /* Calculate the dot and character clock rates. */
    clock_doubled = (pThis->sr[0x01] >> 3) & 1; /* Clock doubling bit. */
    clock_index = (pThis->msr >> 2) & 3;
    char_dots = (pThis->sr[0x01] & 1) ? 8 : 9;  /* 8 or 9 dots per cclk. */

    chars_per_sec = clocks[clock_index] / char_dots;
    Assert(chars_per_sec);  /* Can't possibly be zero. */

    htotal_cclks <<= clock_doubled;

    /* Calculate the number of cclks per entire frame. */
    r->frame_cclks = vtotal_lines * htotal_cclks;
    Assert(r->frame_cclks); /* Can't possibly be zero. */

    if (r->v_freq_hz) { /* Could be set to emulate a specific rate. */
        r->cclk_ns = 1000000000 / (r->frame_cclks * r->v_freq_hz);
    } else {
        r->cclk_ns = 1000000000 / chars_per_sec;
    }
    Assert(r->cclk_ns);
    r->frame_ns = r->frame_cclks * r->cclk_ns;

    /* Calculate timings in cclks/lines. Stored but not directly used. */
    r->hb_start = hblank_start_cclk + hblank_skew_cclks;
    r->hb_end   = hblank_start_cclk + hblank_width + hblank_skew_cclks;
    r->h_total  = htotal_cclks;
    Assert(r->h_total);     /* Can't possibly be zero. */

    r->vb_start = vblank_start_line;
    r->vb_end   = vblank_start_line + vblank_width + 1;
    r->vs_start = vsync_start_line;
    r->vs_end   = vsync_start_line + vsync_width + 1;

    /* Calculate timings in nanoseconds. For easier comparisons, the frame
     * is considered to start at the beginning of the vertical and horizontal
     * blanking period.
     */
    r->h_total_ns  = htotal_cclks * r->cclk_ns;
    r->hb_end_ns   = hblank_width * r->cclk_ns;
    r->vb_end_ns   = vblank_width * r->h_total_ns;
    r->vs_start_ns = (r->vs_start - r->vb_start) * r->h_total_ns;
    r->vs_end_ns   = (r->vs_end   - r->vb_start) * r->h_total_ns;
    Assert(r->h_total_ns);  /* See h_total. */
}

static uint8_t vga_retrace(PPDMDEVINS pDevIns, PVGASTATE pThis)
{
    vga_retrace_s   *r = &pThis->retrace_state;

    if (r->frame_ns) {
        uint8_t     val = pThis->st01 & ~(ST01_V_RETRACE | ST01_DISP_ENABLE);
        unsigned    cur_frame_ns, cur_line_ns;
        uint64_t    time_ns;

        time_ns = PDMDevHlpTMTimeVirtGetNano(pDevIns);

        /* Determine the time within the frame. */
        cur_frame_ns = time_ns % r->frame_ns;

        /* See if we're in the vertical blanking period... */
        if (cur_frame_ns < r->vb_end_ns) {
            val |= ST01_DISP_ENABLE;
            /* ... and additionally in the vertical sync period. */
            if (cur_frame_ns >= r->vs_start_ns && cur_frame_ns <= r->vs_end_ns)
                val |= ST01_V_RETRACE;
        } else {
            /* Determine the time within the current scanline. */
            cur_line_ns = cur_frame_ns % r->h_total_ns;
            /* See if we're in the horizontal blanking period. */
            if (cur_line_ns < r->hb_end_ns)
                val |= ST01_DISP_ENABLE;
        }
        return val;
    } else {
        return pThis->st01 ^ (ST01_V_RETRACE | ST01_DISP_ENABLE);
    }
}

static int vga_ioport_invalid(PVGASTATE pThis, uint32_t addr)
{
    if (pThis->msr & MSR_COLOR_EMULATION) {
        /* Color */
        return (addr >= 0x3b0 && addr <= 0x3bf);
    } else {
        /* Monochrome */
        return (addr >= 0x3d0 && addr <= 0x3df);
    }
}

static uint32_t vga_ioport_read(PPDMDEVINS pDevIns, PVGASTATE pThis, uint32_t addr)
{
    int val, index;

    /* check port range access depending on color/monochrome mode */
    if (vga_ioport_invalid(pThis, addr)) {
        val = 0xff;
        Log(("VGA: following read ignored\n"));
    } else {
        switch(addr) {
        case 0x3c0:
            val = pThis->ar_index;
            break;
        case 0x3c1:
            index = pThis->ar_index & 0x1f;
            if (index < 21)
                val = pThis->ar[index];
            else
                val = 0;
            break;
        case 0x3c2:
            val = pThis->st00;
            break;
        case 0x3c4:
            val = pThis->sr_index;
            break;
        case 0x3c5:
            val = pThis->sr[pThis->sr_index];
            Log2(("vga: read SR%x = 0x%02x\n", pThis->sr_index, val));
            break;
        case 0x3c7:
            val = pThis->dac_state;
            break;
        case 0x3c8:
            val = pThis->dac_write_index;
            break;
        case 0x3c9:
            Assert(pThis->dac_sub_index < 3);
            val = pThis->palette[pThis->dac_read_index * 3 + pThis->dac_sub_index];
            if (++pThis->dac_sub_index == 3) {
                pThis->dac_sub_index = 0;
                pThis->dac_read_index++;
            }
            break;
        case 0x3ca:
            val = pThis->fcr;
            break;
        case 0x3cc:
            val = pThis->msr;
            break;
        case 0x3ce:
            val = pThis->gr_index;
            break;
        case 0x3cf:
            val = pThis->gr[pThis->gr_index];
            Log2(("vga: read GR%x = 0x%02x\n", pThis->gr_index, val));
            break;
        case 0x3b4:
        case 0x3d4:
            val = pThis->cr_index;
            break;
        case 0x3b5:
        case 0x3d5:
            val = pThis->cr[pThis->cr_index];
            Log2(("vga: read CR%x = 0x%02x\n", pThis->cr_index, val));
            break;
        case 0x3ba:
        case 0x3da:
            val = pThis->st01 = vga_retrace(pDevIns, pThis);
            pThis->ar_flip_flop = 0;
            break;
        default:
            val = 0x00;
            break;
        }
    }
    Log(("VGA: read addr=0x%04x data=0x%02x\n", addr, val));
    return val;
}

static void vga_ioport_write(PPDMDEVINS pDevIns, PVGASTATE pThis, uint32_t addr, uint32_t val)
{
    int index;

    Log(("VGA: write addr=0x%04x data=0x%02x\n", addr, val));

    /* check port range access depending on color/monochrome mode */
    if (vga_ioport_invalid(pThis, addr)) {
        Log(("VGA: previous write ignored\n"));
        return;
    }

    switch(addr) {
    case 0x3c0:
    case 0x3c1:
        if (pThis->ar_flip_flop == 0) {
            val &= 0x3f;
            pThis->ar_index = val;
        } else {
            index = pThis->ar_index & 0x1f;
            switch(index) {
            case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
            case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                pThis->ar[index] = val & 0x3f;
                break;
            case 0x10:
                pThis->ar[index] = val & ~0x10;
                break;
            case 0x11:
                pThis->ar[index] = val;
                break;
            case 0x12:
                pThis->ar[index] = val & ~0xc0;
                break;
            case 0x13:
                pThis->ar[index] = val & ~0xf0;
                break;
            case 0x14:
                pThis->ar[index] = val & ~0xf0;
                break;
            default:
                break;
            }
        }
        pThis->ar_flip_flop ^= 1;
        break;
    case 0x3c2:
        pThis->msr = val & ~0x10;
        if (pThis->fRealRetrace)
            vga_update_retrace_state(pThis);
        /* The two clock select bits also determine which of the four switches
         * is reflected in bit 4 of Input Status Register 0.
         * This is EGA compatible behavior. See the IBM EGA Tech Ref.
         */
        pThis->st00 = (pThis->st00 & ~0x10) | ((EGA_SWITCHES >> ((val >> 2) & 0x3) & 0x10));
        break;
    case 0x3c4:
        pThis->sr_index = val & 7;
        break;
    case 0x3c5:
        Log2(("vga: write SR%x = 0x%02x\n", pThis->sr_index, val));
        pThis->sr[pThis->sr_index] = val & sr_mask[pThis->sr_index];
        /* Allow SR07 to disable VBE. */
        if (pThis->sr_index == 0x07 && !(val & 1))
        {
            pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] = VBE_DISPI_DISABLED;
            pThis->bank_offset = 0;
        }
        if (pThis->fRealRetrace && pThis->sr_index == 0x01)
            vga_update_retrace_state(pThis);
#ifndef IN_RC
        /* The VGA region is (could be) affected by this change; reset all aliases we've created. */
        if (    pThis->sr_index == 4 /* mode */
            ||  pThis->sr_index == 2 /* plane mask */)
        {
            if (pThis->bmPageRemappedVGA != 0)
            {
                PDMDevHlpMmioResetRegion(pDevIns, pThis->hMmioLegacy);
                STAM_COUNTER_INC(&pThis->StatMapReset);
                vgaResetRemapped(pThis);
            }
        }
#endif
        break;
    case 0x3c7:
        pThis->dac_read_index = val;
        pThis->dac_sub_index = 0;
        pThis->dac_state = 3;
        break;
    case 0x3c8:
        pThis->dac_write_index = val;
        pThis->dac_sub_index = 0;
        pThis->dac_state = 0;
        break;
    case 0x3c9:
        Assert(pThis->dac_sub_index < 3);
        pThis->dac_cache[pThis->dac_sub_index] = val;
        if (++pThis->dac_sub_index == 3) {
            memcpy(&pThis->palette[pThis->dac_write_index * 3], pThis->dac_cache, 3);
            pThis->dac_sub_index = 0;
            pThis->dac_write_index++;
        }
        break;
    case 0x3ce:
        pThis->gr_index = val & 0x0f;
        break;
    case 0x3cf:
        Log2(("vga: write GR%x = 0x%02x\n", pThis->gr_index, val));
        Assert(pThis->gr_index < RT_ELEMENTS(gr_mask));
        pThis->gr[pThis->gr_index] = val & gr_mask[pThis->gr_index];

#ifndef IN_RC
        /* The VGA region is (could be) affected by this change; reset all aliases we've created. */
        if (pThis->gr_index == 6 /* memory map mode */)
        {
            if (pThis->bmPageRemappedVGA != 0)
            {
                PDMDevHlpMmioResetRegion(pDevIns, pThis->hMmioLegacy);
                STAM_COUNTER_INC(&pThis->StatMapReset);
                vgaResetRemapped(pThis);
            }
        }
#endif
        break;

    case 0x3b4:
    case 0x3d4:
        pThis->cr_index = val;
        break;
    case 0x3b5:
    case 0x3d5:
        Log2(("vga: write CR%x = 0x%02x\n", pThis->cr_index, val));
        /* handle CR0-7 protection */
        if ((pThis->cr[0x11] & 0x80) && pThis->cr_index <= 7) {
            /* can always write bit 4 of CR7 */
            if (pThis->cr_index == 7)
                pThis->cr[7] = (pThis->cr[7] & ~0x10) | (val & 0x10);
            return;
        }
        pThis->cr[pThis->cr_index] = val;

        if (pThis->fRealRetrace) {
            /* The following registers are only updated during a mode set. */
            switch(pThis->cr_index) {
            case 0x00:
            case 0x02:
            case 0x03:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x09:
            case 0x10:
            case 0x11:
            case 0x15:
            case 0x16:
                vga_update_retrace_state(pThis);
                break;
            }
        }
        break;
    case 0x3ba:
    case 0x3da:
        pThis->fcr = val & 0x10;
        break;
    }
}

#ifdef CONFIG_BOCHS_VBE

static uint32_t vbe_read_cfg(PVGASTATE pThis)
{
    const uint16_t u16Cfg = pThis->vbe_regs[VBE_DISPI_INDEX_CFG];
    const uint16_t u16Id = u16Cfg & VBE_DISPI_CFG_MASK_ID;
    const bool fQuerySupport = RT_BOOL(u16Cfg & VBE_DISPI_CFG_MASK_SUPPORT);

    uint32_t val = 0;
    switch (u16Id)
    {
        case VBE_DISPI_CFG_ID_VERSION:   val = 1; break;
        case VBE_DISPI_CFG_ID_VRAM_SIZE: val = pThis->vram_size; break;
        case VBE_DISPI_CFG_ID_3D:        val = pThis->f3DEnabled; break;
# ifdef VBOX_WITH_VMSVGA
        case VBE_DISPI_CFG_ID_VMSVGA:    val = pThis->fVMSVGAEnabled; break;
        case VBE_DISPI_CFG_ID_VMSVGA_DX: val = pThis->fVMSVGA10; break;
# endif
        default:
           return 0; /* Not supported. */
    }

    return fQuerySupport ? 1 : val;
}

static uint32_t vbe_ioport_read_index(PVGASTATE pThis, uint32_t addr)
{
    uint32_t val = pThis->vbe_index;
    NOREF(addr);
    return val;
}

static uint32_t vbe_ioport_read_data(PVGASTATE pThis, uint32_t addr)
{
    uint32_t val;
    NOREF(addr);

    uint16_t const idxVbe = pThis->vbe_index;
    if (idxVbe < VBE_DISPI_INDEX_NB)
    {
        RT_UNTRUSTED_VALIDATED_FENCE();
        if (pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_GETCAPS)
        {
            switch (idxVbe)
            {
                /* XXX: do not hardcode ? */
                case VBE_DISPI_INDEX_XRES:
                    val = VBE_DISPI_MAX_XRES;
                    break;
                case VBE_DISPI_INDEX_YRES:
                    val = VBE_DISPI_MAX_YRES;
                    break;
                case VBE_DISPI_INDEX_BPP:
                    val = VBE_DISPI_MAX_BPP;
                    break;
                default:
                    Assert(idxVbe < VBE_DISPI_INDEX_NB);
                    val = pThis->vbe_regs[idxVbe];
                    break;
            }
        }
        else
        {
            switch (idxVbe)
            {
                case VBE_DISPI_INDEX_VBOX_VIDEO:
                    /* Reading from the port means that the old additions are requesting the number of monitors. */
                    val = 1;
                    break;
                case VBE_DISPI_INDEX_CFG:
                    val = vbe_read_cfg(pThis);
                    break;
                default:
                    Assert(idxVbe < VBE_DISPI_INDEX_NB);
                    val = pThis->vbe_regs[idxVbe];
                    break;
            }
        }
    }
    else
        val = 0;
    Log(("VBE: read index=0x%x val=0x%x\n", idxVbe, val));
    return val;
}

# define VBE_PITCH_ALIGN    4       /* Align pitch to 32 bits - Qt requires that. */

/* Calculate scanline pitch based on bit depth and width in pixels. */
static uint32_t calc_line_pitch(uint16_t bpp, uint16_t width)
{
    uint32_t    pitch, aligned_pitch;

    if (bpp <= 4)
        pitch = width >> 1;
    else
        pitch = width * ((bpp + 7) >> 3);

    /* Align the pitch to some sensible value. */
    aligned_pitch = (pitch + (VBE_PITCH_ALIGN - 1)) & ~(VBE_PITCH_ALIGN - 1);
    if (aligned_pitch != pitch)
        Log(("VBE: Line pitch %d aligned to %d bytes\n", pitch, aligned_pitch));

    return aligned_pitch;
}

static void recalculate_data(PVGASTATE pThis)
{
    uint16_t cBPP        = pThis->vbe_regs[VBE_DISPI_INDEX_BPP];
    uint16_t cVirtWidth  = pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH];
    uint16_t cX          = pThis->vbe_regs[VBE_DISPI_INDEX_XRES];
    if (!cBPP || !cX)
        return;  /* Not enough data has been set yet. */
    uint32_t cbLinePitch = calc_line_pitch(cBPP, cVirtWidth);
    if (!cbLinePitch)
        cbLinePitch      = calc_line_pitch(cBPP, cX);
    if (!cbLinePitch)
        return;
    uint32_t cVirtHeight = pThis->vram_size / cbLinePitch;
    uint16_t offX        = pThis->vbe_regs[VBE_DISPI_INDEX_X_OFFSET];
    uint16_t offY        = pThis->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET];
    uint32_t offStart    = cbLinePitch * offY;
    if (cBPP == 4)
        offStart += offX >> 1;
    else
        offStart += offX * ((cBPP + 7) >> 3);

    pThis->vbe_line_offset = RT_MIN(cbLinePitch, pThis->vram_size);
    pThis->vbe_start_addr  = RT_MIN(offStart, pThis->vram_size) >> 2;

    /* The VBE_DISPI_INDEX_VIRT_HEIGHT is used to prevent setting resolution bigger than
     * the VRAM size permits. It is used instead of VBE_DISPI_INDEX_YRES *only* in case
     * pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT] < pThis->vbe_regs[VBE_DISPI_INDEX_YRES].
     * Note that VBE_DISPI_INDEX_VIRT_HEIGHT has to be clipped to UINT16_MAX, which happens
     * with small resolutions and big VRAM. */
    pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT] = cVirtHeight >= UINT16_MAX ? UINT16_MAX : (uint16_t)cVirtHeight;
}

static void vbe_ioport_write_index(PVGASTATE pThis, uint32_t addr, uint32_t val)
{
    pThis->vbe_index = val;
    NOREF(addr);
}

static VBOXSTRICTRC vbe_ioport_write_data(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t addr, uint32_t val)
{
    uint32_t max_bank;
    RT_NOREF(pThisCC, addr);

    uint16_t const idxVbe = pThis->vbe_index;
    if (idxVbe <= VBE_DISPI_INDEX_NB)
    {
        RT_UNTRUSTED_VALIDATED_FENCE();

        bool fRecalculate = false;
        Log(("VBE: write index=0x%x val=0x%x\n", idxVbe, val));
        switch(idxVbe) {
        case VBE_DISPI_INDEX_ID:
            if (val == VBE_DISPI_ID0 ||
                val == VBE_DISPI_ID1 ||
                val == VBE_DISPI_ID2 ||
                val == VBE_DISPI_ID3 ||
                val == VBE_DISPI_ID4 ||
                /* VBox extensions. */
                val == VBE_DISPI_ID_VBOX_VIDEO ||
                val == VBE_DISPI_ID_ANYX       ||
# ifdef VBOX_WITH_HGSMI
                val == VBE_DISPI_ID_HGSMI      ||
# endif
                val == VBE_DISPI_ID_CFG)
            {
                pThis->vbe_regs[VBE_DISPI_INDEX_ID] = val;
            }
            break;
        case VBE_DISPI_INDEX_XRES:
            if (val <= VBE_DISPI_MAX_XRES)
            {
                pThis->vbe_regs[VBE_DISPI_INDEX_XRES] = val;
                pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] = val;
                fRecalculate = true;
            }
            break;
        case VBE_DISPI_INDEX_YRES:
            if (val <= VBE_DISPI_MAX_YRES)
                pThis->vbe_regs[VBE_DISPI_INDEX_YRES] = val;
            break;
        case VBE_DISPI_INDEX_BPP:
            if (val == 0)
                val = 8;
            if (val == 4 || val == 8 || val == 15 ||
                val == 16 || val == 24 || val == 32) {
                pThis->vbe_regs[VBE_DISPI_INDEX_BPP] = val;
                fRecalculate = true;
            }
            break;
        case VBE_DISPI_INDEX_BANK:
            if (pThis->vbe_regs[VBE_DISPI_INDEX_BPP] <= 4)
                max_bank = pThis->vbe_bank_max >> 2;    /* Each bank really covers 256K */
            else
                max_bank = pThis->vbe_bank_max;
            /* Old software may pass garbage in the high byte of bank. If the maximum
             * bank fits into a single byte, toss the high byte the user supplied.
             */
            if (max_bank < 0x100)
                val &= 0xff;
            if (val > max_bank)
                val = max_bank;
            pThis->vbe_regs[VBE_DISPI_INDEX_BANK] = val;
            pThis->bank_offset = (val << 16);

# ifndef IN_RC
            /* The VGA region is (could be) affected by this change; reset all aliases we've created. */
            if (pThis->bmPageRemappedVGA != 0)
            {
                PDMDevHlpMmioResetRegion(pDevIns, pThis->hMmioLegacy);
                STAM_COUNTER_INC(&pThis->StatMapReset);
                vgaResetRemapped(pThis);
            }
# endif
            break;

        case VBE_DISPI_INDEX_ENABLE:
# ifndef IN_RING3
            return VINF_IOM_R3_IOPORT_WRITE;
# else
        {
            if ((val & VBE_DISPI_ENABLED) &&
                !(pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED)) {
                int h, shift_control;
                /* Check the values before we screw up with a resolution which is too big or small. */
                size_t cb;
                if (pThis->vbe_regs[VBE_DISPI_INDEX_BPP] == 4)
                    cb = pThis->vbe_regs[VBE_DISPI_INDEX_XRES] >> 1;
                else
                    cb = pThis->vbe_regs[VBE_DISPI_INDEX_XRES] * ((pThis->vbe_regs[VBE_DISPI_INDEX_BPP] + 7) >> 3);
                cb *= pThis->vbe_regs[VBE_DISPI_INDEX_YRES];
                uint16_t cVirtWidth = pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH];
                if (!cVirtWidth)
                    cVirtWidth = pThis->vbe_regs[VBE_DISPI_INDEX_XRES];
                if (    !cVirtWidth
                    ||  !pThis->vbe_regs[VBE_DISPI_INDEX_YRES]
                    ||  cb > pThis->vram_size)
                {
                    ASSERT_GUEST_MSG_FAILED(("VIRT WIDTH=%d YRES=%d cb=%d vram_size=%d\n",
                                             pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH], pThis->vbe_regs[VBE_DISPI_INDEX_YRES],
                                             cb, pThis->vram_size));
                    return VINF_SUCCESS; /* Note: silent failure like before */
                }

                /* When VBE interface is enabled, it is reset. */
                pThis->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] = 0;
                pThis->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] = 0;
                fRecalculate = true;

                /* clear the screen (should be done in BIOS) */
                if (!(val & VBE_DISPI_NOCLEARMEM)) {
                    uint16_t cY = RT_MIN(pThis->vbe_regs[VBE_DISPI_INDEX_YRES],
                                         pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT]);
                    uint16_t cbLinePitch = pThis->vbe_line_offset;
                    memset(pThisCC->pbVRam, 0,
                           cY * cbLinePitch);
                }

                /* we initialize the VGA graphic mode (should be done
                   in BIOS) */
                pThis->gr[0x06] = (pThis->gr[0x06] & ~0x0c) | 0x05; /* graphic mode + memory map 1 */
                pThis->cr[0x17] |= 3; /* no CGA modes */
                pThis->cr[0x13] = pThis->vbe_line_offset >> 3;
                /* width */
                pThis->cr[0x01] = (cVirtWidth >> 3) - 1;
                /* height (only meaningful if < 1024) */
                h = pThis->vbe_regs[VBE_DISPI_INDEX_YRES] - 1;
                pThis->cr[0x12] = h;
                pThis->cr[0x07] = (pThis->cr[0x07] & ~0x42) |
                    ((h >> 7) & 0x02) | ((h >> 3) & 0x40);
                /* line compare to 1023 */
                pThis->cr[0x18] = 0xff;
                pThis->cr[0x07] |= 0x10;
                pThis->cr[0x09] |= 0x40;

                if (pThis->vbe_regs[VBE_DISPI_INDEX_BPP] == 4) {
                    shift_control = 0;
                    pThis->sr[0x01] &= ~8; /* no double line */
                } else {
                    shift_control = 2;
                    pThis->sr[4] |= 0x08; /* set chain 4 mode */
                    pThis->sr[2] |= 0x0f; /* activate all planes */
                    /* Indicate non-VGA mode in SR07. */
                    pThis->sr[7] |= 1;
                }
                pThis->gr[0x05] = (pThis->gr[0x05] & ~0x60) | (shift_control << 5);
                pThis->cr[0x09] &= ~0x9f; /* no double scan */
                /* sunlover 30.05.2007
                 * The ar_index remains with bit 0x20 cleared after a switch from fullscreen
                 * DOS mode on Windows XP guest. That leads to GMODE_BLANK in vgaR3UpdateDisplay.
                 * But the VBE mode is graphics, so not a blank anymore.
                 */
                pThis->ar_index |= 0x20;
            } else {
                /* XXX: the bios should do that */
                /* sunlover 21.12.2006
                 * Here is probably more to reset. When this was executed in GC
                 * then the *update* functions could not detect a mode change.
                 * Or may be these update function should take the pThis->vbe_regs[pThis->vbe_index]
                 * into account when detecting a mode change.
                 *
                 * The 'mode reset not detected' problem is now fixed by executing the
                 * VBE_DISPI_INDEX_ENABLE case always in RING3 in order to call the
                 * LFBChange callback.
                 */
                pThis->bank_offset = 0;
            }
            pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] = val;
            /*
             * LFB video mode is either disabled or changed. Notify the display
             * and reset VBVA.
             */
            pThisCC->pDrv->pfnLFBModeChange(pThisCC->pDrv, (val & VBE_DISPI_ENABLED) != 0);
#  ifdef VBOX_WITH_HGSMI
            VBVAOnVBEChanged(pThis, pThisCC);
#  endif

            /* The VGA region is (could be) affected by this change; reset all aliases we've created. */
            if (pThis->bmPageRemappedVGA != 0)
            {
                PDMDevHlpMmioResetRegion(pDevIns, pThis->hMmioLegacy);
                STAM_COUNTER_INC(&pThis->StatMapReset);
                vgaResetRemapped(pThis);
            }
            break;
        }
# endif /* IN_RING3 */
        case VBE_DISPI_INDEX_VIRT_WIDTH:
        case VBE_DISPI_INDEX_X_OFFSET:
        case VBE_DISPI_INDEX_Y_OFFSET:
            {
                pThis->vbe_regs[idxVbe] = val;
                fRecalculate = true;
            }
            break;
        case VBE_DISPI_INDEX_VBOX_VIDEO:
# ifndef IN_RING3
            return VINF_IOM_R3_IOPORT_WRITE;
# else
            /* Changes in the VGA device are minimal. The device is bypassed. The driver does all work. */
            if (val == VBOX_VIDEO_DISABLE_ADAPTER_MEMORY)
                pThisCC->pDrv->pfnProcessAdapterData(pThisCC->pDrv, NULL, 0);
            else if (val == VBOX_VIDEO_INTERPRET_ADAPTER_MEMORY)
                pThisCC->pDrv->pfnProcessAdapterData(pThisCC->pDrv, pThisCC->pbVRam, pThis->vram_size);
            else if ((val & 0xFFFF0000) == VBOX_VIDEO_INTERPRET_DISPLAY_MEMORY_BASE)
                pThisCC->pDrv->pfnProcessDisplayData(pThisCC->pDrv, pThisCC->pbVRam, val & 0xFFFF);
# endif /* IN_RING3 */
            break;
        case VBE_DISPI_INDEX_CFG:
            pThis->vbe_regs[VBE_DISPI_INDEX_CFG] = val;
            break;
        default:
            break;
        }

        if (fRecalculate)
            recalculate_data(pThis);
    }
    return VINF_SUCCESS;
}

#endif /* CONFIG_BOCHS_VBE */

/* called for accesses between 0xa0000 and 0xc0000 */
static uint32_t vga_mem_readb(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, RTGCPHYS addr, int *prc)
{
    int plane;
    uint32_t ret;

    Log3(("vga: read [0x%x] -> ", addr));

#ifdef VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RZ
    /* VMSVGA keeps the VGA and SVGA framebuffers separate unlike this boch-based
       VGA implementation, so we fake it by going to ring-3 and using a heap buffer.  */
    if (!pThis->svga.fEnabled)
    { /*likely*/ }
    else
    {
        *prc = VINF_IOM_R3_MMIO_READ;
        return 0;
    }
#endif


    /* convert to VGA memory offset */
    addr &= 0x1ffff;
#ifndef IN_RC
    RTGCPHYS const offMmio = addr; /* save original MMIO range offset  */
#endif

    int const memory_map_mode = (pThis->gr[6] >> 2) & 3;
    switch(memory_map_mode) {
    case 0:
        break;
    case 1:
        if (addr >= 0x10000)
            return 0xff;
        addr += pThis->bank_offset;
        break;
    case 2:
        addr -= 0x10000;
        if (addr >= 0x8000)
            return 0xff;
        break;
    default:
    case 3:
        addr -= 0x18000;
        if (addr >= 0x8000)
            return 0xff;
        break;
    }

    if (pThis->sr[4] & 0x08) {
        /* chain 4 mode : simplest access */
#ifndef IN_RC
        /* If all planes are accessible, then map the page to the frame buffer and make it writable. */
        if (   (pThis->sr[2] & 3) == 3
            && !vgaIsRemapped(pThis, offMmio)
            && pThis->GCPhysVRAM)
        {
            /** @todo only allow read access (doesn't work now) */
            STAM_COUNTER_INC(&pThis->StatMapPage);
            PDMDevHlpMmioMapMmio2Page(pDevIns, pThis->hMmioLegacy, offMmio, pThis->hMmio2VRam, addr, X86_PTE_RW | X86_PTE_P);
            /* Set as dirty as write accesses won't be noticed now. */
            vgaR3MarkDirty(pThis, addr);
            vgaMarkRemapped(pThis, offMmio);
        }
#endif /* !IN_RC */
        VERIFY_VRAM_READ_OFF_RETURN(pThis, addr, *prc);
#ifdef VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RING3
        ret = !pThis->svga.fEnabled            ? pThisCC->pbVRam[addr]
            : addr < VMSVGA_VGA_FB_BACKUP_SIZE ? pThisCC->svga.pbVgaFrameBufferR3[addr] : 0xff;
#else
        ret = pThisCC->pbVRam[addr];
#endif
    } else if (!(pThis->sr[4] & 0x04)) {    /* Host access is controlled by SR4, not GR5! */
        /* odd/even mode (aka text mode mapping) */
        plane = (pThis->gr[4] & 2) | (addr & 1);
        /* See the comment for a similar line in vga_mem_writeb. */
        RTGCPHYS off = ((addr & ~1) * 4) | plane;
        VERIFY_VRAM_READ_OFF_RETURN(pThis, off, *prc);
#ifdef VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RING3
        ret = !pThis->svga.fEnabled           ? pThisCC->pbVRam[off]
            : off < VMSVGA_VGA_FB_BACKUP_SIZE ? pThisCC->svga.pbVgaFrameBufferR3[off] : 0xff;
#else
        ret = pThisCC->pbVRam[off];
#endif
    } else {
        /* standard VGA latched access */
        VERIFY_VRAM_READ_OFF_RETURN(pThis, addr * 4 + 3, *prc);
#ifdef VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RING3
        pThis->latch = !pThis->svga.fEnabled                    ? ((uint32_t *)pThisCC->pbVRam)[addr]
                     : addr * 4 + 3 < VMSVGA_VGA_FB_BACKUP_SIZE ? ((uint32_t *)pThisCC->svga.pbVgaFrameBufferR3)[addr] : UINT32_MAX;
#else
        pThis->latch = ((uint32_t *)pThisCC->pbVRam)[addr];
#endif
        if (!(pThis->gr[5] & 0x08)) {
            /* read mode 0 */
            plane = pThis->gr[4];
            ret = GET_PLANE(pThis->latch, plane);
        } else {
            /* read mode 1 */
            ret = (pThis->latch ^ mask16[pThis->gr[2]]) & mask16[pThis->gr[7]];
            ret |= ret >> 16;
            ret |= ret >> 8;
            ret = (~ret) & 0xff;
        }
    }
    Log3((" 0x%02x\n", ret));
    return ret;
}

/**
 * called for accesses between 0xa0000 and 0xc0000
 */
static VBOXSTRICTRC vga_mem_writeb(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, RTGCPHYS addr, uint32_t val)
{
    int plane, write_mode, b, func_select, mask;
    uint32_t write_mask, bit_mask, set_mask;

    Log3(("vga: [0x%x] = 0x%02x\n", addr, val));

#ifdef VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RZ
    /* VMSVGA keeps the VGA and SVGA framebuffers separate unlike this boch-based
       VGA implementation, so we fake it by going to ring-3 and using a heap buffer.  */
    if (!pThis->svga.fEnabled) { /*likely*/ }
    else                       return VINF_IOM_R3_MMIO_WRITE;
#endif

    /* convert to VGA memory offset */
    addr &= 0x1ffff;
#ifndef IN_RC
    RTGCPHYS const offMmio = addr; /* save original MMIO range offset  */
#endif

    int const memory_map_mode = (pThis->gr[6] >> 2) & 3;
    switch(memory_map_mode) {
    case 0:
        break;
    case 1:
        if (addr >= 0x10000)
            return VINF_SUCCESS;
        addr += pThis->bank_offset;
        break;
    case 2:
        addr -= 0x10000;
        if (addr >= 0x8000)
            return VINF_SUCCESS;
        break;
    default:
    case 3:
        addr -= 0x18000;
        if (addr >= 0x8000)
            return VINF_SUCCESS;
        break;
    }

    if (pThis->sr[4] & 0x08) {
        /* chain 4 mode : simplest access */
        plane = addr & 3;
        mask = (1 << plane);
        if (pThis->sr[2] & mask) {
#ifndef IN_RC
            /* If all planes are accessible, then map the page to the frame buffer and make it writable. */
            if (   (pThis->sr[2] & 3) == 3
                && !vgaIsRemapped(pThis, offMmio)
                && pThis->GCPhysVRAM)
            {
                STAM_COUNTER_INC(&pThis->StatMapPage);
                PDMDevHlpMmioMapMmio2Page(pDevIns, pThis->hMmioLegacy, offMmio, pThis->hMmio2VRam, addr, X86_PTE_RW | X86_PTE_P);
                vgaMarkRemapped(pThis, offMmio);
            }
#endif /* !IN_RC */

            VERIFY_VRAM_WRITE_OFF_RETURN(pThis, addr);
#ifdef VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RING3
            if (!pThis->svga.fEnabled)
                pThisCC->pbVRam[addr]      = val;
            else if (addr < VMSVGA_VGA_FB_BACKUP_SIZE)
                pThisCC->svga.pbVgaFrameBufferR3[addr] = val;
            else
            {
                Log(("vga: chain4: out of vmsvga VGA framebuffer bounds! addr=%#x\n", addr));
                return VINF_SUCCESS;
            }
#else
            pThisCC->pbVRam[addr] = val;
#endif
            Log3(("vga: chain4: [0x%x]\n", addr));
            pThis->plane_updated |= mask; /* only used to detect font change */
            vgaR3MarkDirty(pThis, addr);
        }
    } else if (!(pThis->sr[4] & 0x04)) {    /* Host access is controlled by SR4, not GR5! */
        /* odd/even mode (aka text mode mapping); GR4 does not affect writes! */
        plane = addr & 1;
        mask = (1 << plane);
        if (pThis->sr[2] & mask) {
            /* 'addr' is offset in a plane, bit 0 selects the plane.
             * Mask the bit 0, convert plane index to vram offset,
             * that is multiply by the number of planes,
             * and select the plane byte in the vram offset.
             */
            addr = ((addr & ~1) * 4) | plane;
            VERIFY_VRAM_WRITE_OFF_RETURN(pThis, addr);
#ifdef VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RING3
            if (!pThis->svga.fEnabled)
                pThisCC->pbVRam[addr]      = val;
            else if (addr < VMSVGA_VGA_FB_BACKUP_SIZE)
                pThisCC->svga.pbVgaFrameBufferR3[addr] = val;
            else
            {
                Log(("vga: odd/even: out of vmsvga VGA framebuffer bounds! addr=%#x\n", addr));
                return VINF_SUCCESS;
            }
#else
            pThisCC->pbVRam[addr] = val;
#endif
            Log3(("vga: odd/even: [0x%x]\n", addr));
            pThis->plane_updated |= mask; /* only used to detect font change */
            vgaR3MarkDirty(pThis, addr);
        }
    } else {
        /* standard VGA latched access */
        VERIFY_VRAM_WRITE_OFF_RETURN(pThis, addr * 4 + 3);

        write_mode = pThis->gr[5] & 3;
        switch(write_mode) {
        default:
        case 0:
            /* rotate */
            b = pThis->gr[3] & 7;
            val = ((val >> b) | (val << (8 - b))) & 0xff;
            val |= val << 8;
            val |= val << 16;

            /* apply set/reset mask */
            set_mask = mask16[pThis->gr[1]];
            val = (val & ~set_mask) | (mask16[pThis->gr[0]] & set_mask);
            bit_mask = pThis->gr[8];
            break;
        case 1:
            val = pThis->latch;
            goto do_write;
        case 2:
            val = mask16[val & 0x0f];
            bit_mask = pThis->gr[8];
            break;
        case 3:
            /* rotate */
            b = pThis->gr[3] & 7;
            val = (val >> b) | (val << (8 - b));

            bit_mask = pThis->gr[8] & val;
            val = mask16[pThis->gr[0]];
            break;
        }

        /* apply logical operation */
        func_select = pThis->gr[3] >> 3;
        switch(func_select) {
        case 0:
        default:
            /* nothing to do */
            break;
        case 1:
            /* and */
            val &= pThis->latch;
            break;
        case 2:
            /* or */
            val |= pThis->latch;
            break;
        case 3:
            /* xor */
            val ^= pThis->latch;
            break;
        }

        /* apply bit mask */
        bit_mask |= bit_mask << 8;
        bit_mask |= bit_mask << 16;
        val = (val & bit_mask) | (pThis->latch & ~bit_mask);

    do_write:
        /* mask data according to sr[2] */
        mask = pThis->sr[2];
        pThis->plane_updated |= mask; /* only used to detect font change */
        write_mask = mask16[mask];
#ifdef VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RING3
        uint32_t *pu32Dst;
        if (!pThis->svga.fEnabled)
            pu32Dst = &((uint32_t *)pThisCC->pbVRam)[addr];
        else if (addr * 4 + 3 < VMSVGA_VGA_FB_BACKUP_SIZE)
            pu32Dst = &((uint32_t *)pThisCC->svga.pbVgaFrameBufferR3)[addr];
        else
        {
            Log(("vga: latch: out of vmsvga VGA framebuffer bounds! addr=%#x\n", addr));
            return VINF_SUCCESS;
        }
        *pu32Dst = (*pu32Dst & ~write_mask) | (val & write_mask);
#else
        ((uint32_t *)pThisCC->pbVRam)[addr] = (((uint32_t *)pThisCC->pbVRam)[addr] & ~write_mask)
                                                      | (val & write_mask);
#endif
        Log3(("vga: latch: [0x%x] mask=0x%08x val=0x%08x\n", addr * 4, write_mask, val));
        vgaR3MarkDirty(pThis, (addr * 4));
    }

    return VINF_SUCCESS;
}

#ifdef IN_RING3

typedef void vga_draw_glyph8_func(uint8_t *d, int linesize,
                                  const uint8_t *font_ptr, int h,
                                  uint32_t fgcol, uint32_t bgcol,
                                  int dscan);
typedef void vga_draw_glyph9_func(uint8_t *d, int linesize,
                                  const uint8_t *font_ptr, int h,
                                  uint32_t fgcol, uint32_t bgcol, int dup9);
typedef void vga_draw_line_func(PVGASTATE pThis, PVGASTATECC pThisCC, uint8_t *pbDst, const uint8_t *pbSrc, int width);

static inline unsigned int rgb_to_pixel8(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
}

static inline unsigned int rgb_to_pixel15(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

static inline unsigned int rgb_to_pixel16(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static inline unsigned int rgb_to_pixel32(unsigned int r, unsigned int g, unsigned b)
{
    return (r << 16) | (g << 8) | b;
}

#define DEPTH 8
#include "DevVGATmpl.h"

#define DEPTH 15
#include "DevVGATmpl.h"

#define DEPTH 16
#include "DevVGATmpl.h"

#define DEPTH 32
#include "DevVGATmpl.h"

static unsigned int rgb_to_pixel8_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel8(r, g, b);
    col |= col << 8;
    col |= col << 16;
    return col;
}

static unsigned int rgb_to_pixel15_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel15(r, g, b);
    col |= col << 16;
    return col;
}

static unsigned int rgb_to_pixel16_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel16(r, g, b);
    col |= col << 16;
    return col;
}

static unsigned int rgb_to_pixel32_dup(unsigned int r, unsigned int g, unsigned b)
{
    return rgb_to_pixel32(r, g, b);
}

/** return true if the palette was modified */
static bool vgaR3UpdatePalette16(PVGASTATE pThis, PVGASTATER3 pThisCC)
{
    bool full_update = false;
    int i;
    uint32_t v, col, *palette;

    palette = pThis->last_palette;
    for(i = 0; i < 16; i++) {
        v = pThis->ar[i];
        if (pThis->ar[0x10] & 0x80)
            v = ((pThis->ar[0x14] & 0xf) << 4) | (v & 0xf);
        else
            v = ((pThis->ar[0x14] & 0xc) << 4) | (v & 0x3f);
        v = v * 3;
        col = pThisCC->rgb_to_pixel(c6_to_8(pThis->palette[v]),
                                    c6_to_8(pThis->palette[v + 1]),
                                    c6_to_8(pThis->palette[v + 2]));
        if (col != palette[i]) {
            full_update = true;
            palette[i] = col;
        }
    }
    return full_update;
}

/** return true if the palette was modified */
static bool vgaR3UpdatePalette256(PVGASTATE pThis, PVGASTATER3 pThisCC)
{
    bool full_update = false;
    int i;
    uint32_t v, col, *palette;
    int wide_dac;

    palette = pThis->last_palette;
    v = 0;
    wide_dac = (pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & (VBE_DISPI_ENABLED | VBE_DISPI_8BIT_DAC))
             == (VBE_DISPI_ENABLED | VBE_DISPI_8BIT_DAC);
    for(i = 0; i < 256; i++) {
        if (wide_dac)
            col = pThisCC->rgb_to_pixel(pThis->palette[v],
                                        pThis->palette[v + 1],
                                        pThis->palette[v + 2]);
        else
            col = pThisCC->rgb_to_pixel(c6_to_8(pThis->palette[v]),
                                        c6_to_8(pThis->palette[v + 1]),
                                        c6_to_8(pThis->palette[v + 2]));
        if (col != palette[i]) {
            full_update = true;
            palette[i] = col;
        }
        v += 3;
    }
    return full_update;
}

static void vgaR3GetOffsets(PVGASTATE pThis,
                            uint32_t *pline_offset,
                            uint32_t *pstart_addr,
                            uint32_t *pline_compare)
{
    uint32_t start_addr, line_offset, line_compare;
#ifdef CONFIG_BOCHS_VBE
    if (pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        line_offset = pThis->vbe_line_offset;
        start_addr = pThis->vbe_start_addr;
        line_compare = 65535;
    } else
#endif
    {
        /* compute line_offset in bytes */
        line_offset = pThis->cr[0x13];
        line_offset <<= 3;
        if (!(pThis->cr[0x14] & 0x40) && !(pThis->cr[0x17] & 0x40))
        {
            /* Word mode. Used for odd/even modes. */
            line_offset *= 2;
        }

        /* starting address */
        start_addr = pThis->cr[0x0d] | (pThis->cr[0x0c] << 8);

        /* line compare */
        line_compare = pThis->cr[0x18] |
            ((pThis->cr[0x07] & 0x10) << 4) |
            ((pThis->cr[0x09] & 0x40) << 3);
    }
    *pline_offset = line_offset;
    *pstart_addr = start_addr;
    *pline_compare = line_compare;
}

/** update start_addr and line_offset. Return TRUE if modified */
static bool vgaR3UpdateBasicParams(PVGASTATE pThis, PVGASTATER3 pThisCC)
{
    bool full_update = false;
    uint32_t start_addr, line_offset, line_compare;

    pThisCC->get_offsets(pThis, &line_offset, &start_addr, &line_compare);

    if (line_offset != pThis->line_offset ||
        start_addr != pThis->start_addr ||
        line_compare != pThis->line_compare) {
        pThis->line_offset = line_offset;
        pThis->start_addr = start_addr;
        pThis->line_compare = line_compare;
        full_update = true;
    }
    return full_update;
}

static inline int vgaR3GetDepthIndex(int depth)
{
    switch(depth) {
    default:
    case 8:
        return 0;
    case 15:
        return 1;
    case 16:
        return 2;
    case 32:
        return 3;
    }
}

static vga_draw_glyph8_func * const vga_draw_glyph8_table[4] = {
    vga_draw_glyph8_8,
    vga_draw_glyph8_16,
    vga_draw_glyph8_16,
    vga_draw_glyph8_32,
};

static vga_draw_glyph8_func * const vga_draw_glyph16_table[4] = {
    vga_draw_glyph16_8,
    vga_draw_glyph16_16,
    vga_draw_glyph16_16,
    vga_draw_glyph16_32,
};

static vga_draw_glyph9_func * const vga_draw_glyph9_table[4] = {
    vga_draw_glyph9_8,
    vga_draw_glyph9_16,
    vga_draw_glyph9_16,
    vga_draw_glyph9_32,
};

static const uint8_t cursor_glyph[32 * 4] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static const uint8_t empty_glyph[32 * 4] = { 0 };

/**
 * Text mode update
 */
static int vgaR3DrawText(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATER3 pThisCC, bool full_update,
                         bool fFailOnResize, bool reset_dirty, PDMIDISPLAYCONNECTOR *pDrv)
{
    int cx, cy, cheight, cw, ch, cattr, height, width, ch_attr;
    int cx_min, cx_max, linesize, x_incr;
    int cx_min_upd, cx_max_upd, cy_start;
    uint32_t offset, fgcol, bgcol, v, cursor_offset;
    uint8_t *d1, *d, *src, *s1, *dest, *cursor_ptr;
    const uint8_t *font_ptr, *font_base[2];
    int dup9, line_offset, depth_index, dscan;
    uint32_t *palette;
    uint32_t *ch_attr_ptr;
    vga_draw_glyph8_func *vga_draw_glyph8;
    vga_draw_glyph9_func *vga_draw_glyph9;
    uint64_t time_ns;
    bool blink_on, chr_blink_flip, cur_blink_flip;
    bool blink_enabled, blink_do_redraw;
    int uline_pos;
    int s_incr;

    full_update |= vgaR3UpdatePalette16(pThis, pThisCC);
    palette = pThis->last_palette;

    /* compute font data address (in plane 2) */
    v = pThis->sr[3];
    offset = (((v >> 4) & 1) | ((v << 1) & 6)) * 8192 * 4 + 2;
    if (offset != pThis->font_offsets[0]) {
        pThis->font_offsets[0] = offset;
        full_update = true;
    }
    font_base[0] = pThisCC->pbVRam + offset;

    offset = (((v >> 5) & 1) | ((v >> 1) & 6)) * 8192 * 4 + 2;
    font_base[1] = pThisCC->pbVRam + offset;
    if (offset != pThis->font_offsets[1]) {
        pThis->font_offsets[1] = offset;
        full_update = true;
    }
    if (pThis->plane_updated & (1 << 2)) {
        /* if the plane 2 was modified since the last display, it
           indicates the font may have been modified */
        pThis->plane_updated = 0;
        full_update = true;
    }

    /* Underline position */
    uline_pos  = pThis->cr[0x14] & 0x1f;
    if (uline_pos != pThis->last_uline) {
        pThis->last_uline = uline_pos;
        full_update = true;
    }

    blink_enabled = !!(pThis->ar[0x10] & 0x08); /* Attribute controller blink enable. */
    if (blink_enabled != pThis->last_blink) {
        pThis->last_blink = blink_enabled;
        full_update = true;
    }

    full_update |= vgaR3UpdateBasicParams(pThis, pThisCC);

    /* Evaluate word/byte mode. Need to count by 4 because text is only in plane 0. */
    s_incr = pThis->cr[0x17] & 0x40 ? 4 : 8;

    unsigned addr_mask;
    if (!(pThis->cr[0x17] & 0x40) && !(pThis->cr[0x17] & 0x20))
        addr_mask = 0xffff;     /* Wrap at 64K, for CGA and 64K EGA compatibility. */
    else
        addr_mask = 0x3ffff;    /* Wrap at 256K, standard VGA. */

    line_offset = pThis->line_offset;
    s1 = pThisCC->pbVRam + ((pThis->start_addr * s_incr) & addr_mask);

    /* double scanning - not for 9-wide modes */
    dscan = (pThis->cr[9] >> 7) & 1;

    /* total width & height */
    cheight = (pThis->cr[9] & 0x1f) + 1;
    cw = 8;
    if (!(pThis->sr[1] & 0x01))
        cw = 9;
    if (pThis->sr[1] & 0x08)
        cw = 16; /* NOTE: no 18 pixel wide */
    width = (pThis->cr[0x01] + 1);
    if (pThis->cr[0x06] == 100) {
        /* ugly hack for CGA 160x100x16 - explain me the logic */
        height = 100;
    } else {
        height = pThis->cr[0x12] |
            ((pThis->cr[0x07] & 0x02) << 7) |
            ((pThis->cr[0x07] & 0x40) << 3);
        height = (height + 1) / cheight;
    }
    /** @todo r=michaln This conditional is questionable; we should be able
     * to draw whatver the guest asks for. */
    if ((height * width) > CH_ATTR_SIZE) {
        /* better than nothing: exit if transient size is too big */
        return VINF_SUCCESS;
    }

    int const scr_width = width * cw;
    int const scr_height = height * cheight;
    if (width != (int)pThis->last_width || height != (int)pThis->last_height ||
        cw != pThis->last_cw || cheight != pThis->last_ch ||
        scr_width != (int)pDrv->cx || scr_height != (int)pDrv->cy) {
        if (fFailOnResize)
        {
            /* The caller does not want to call the pfnResize. */
            return VERR_TRY_AGAIN;
        }
        pThis->last_scr_width = scr_width;
        pThis->last_scr_height = scr_height;
        /* For text modes the direct use of guest VRAM is not implemented, so bpp and cbLine are 0 here. */
        int rc = pDrv->pfnResize(pDrv, 0, NULL, 0, pThis->last_scr_width, pThis->last_scr_height);
        pThis->last_width = width;
        pThis->last_height = height;
        pThis->last_ch = cheight;
        pThis->last_cw = cw;
        full_update = true;
        if (rc == VINF_VGA_RESIZE_IN_PROGRESS)
            return rc;
        AssertRCReturn(rc, rc);
    }
    AssertReturn(scr_width == (int)pDrv->cx && scr_height == (int)pDrv->cy, VERR_INVALID_STATE);

    x_incr = cw * ((pDrv->cBits + 7) >> 3);

    cursor_offset = ((pThis->cr[0x0e] << 8) | pThis->cr[0x0f]) - pThis->start_addr;
    if (cursor_offset != pThis->cursor_offset ||
        pThis->cr[0xa] != pThis->cursor_start ||
        pThis->cr[0xb] != pThis->cursor_end) {
      /* if the cursor position changed, we update the old and new
         chars */
        if (pThis->cursor_offset < CH_ATTR_SIZE)
            pThis->last_ch_attr[pThis->cursor_offset] = UINT32_MAX;
        if (cursor_offset < CH_ATTR_SIZE)
            pThis->last_ch_attr[cursor_offset] = UINT32_MAX;
        pThis->cursor_offset = cursor_offset;
        pThis->cursor_start = pThis->cr[0xa];
        pThis->cursor_end = pThis->cr[0xb];
    }
    cursor_ptr = pThisCC->pbVRam + (((pThis->start_addr + cursor_offset) * s_incr) & addr_mask);
    depth_index = vgaR3GetDepthIndex(pDrv->cBits);
    if (cw == 16)
        vga_draw_glyph8 = vga_draw_glyph16_table[depth_index];
    else
        vga_draw_glyph8 = vga_draw_glyph8_table[depth_index];
    vga_draw_glyph9 = vga_draw_glyph9_table[depth_index];

    dest = pDrv->pbData;
    linesize = pDrv->cbScanline;
    ch_attr_ptr = pThis->last_ch_attr;
    cy_start = -1;
    cx_max_upd = -1;
    cx_min_upd = width;

    /* Figure out if we're in the visible period of the blink cycle. */
    time_ns  = PDMDevHlpTMTimeVirtGetNano(pDevIns);
    blink_on = (time_ns % VGA_BLINK_PERIOD_FULL) < VGA_BLINK_PERIOD_ON;
    chr_blink_flip = false;
    cur_blink_flip = false;
    if (pThis->last_chr_blink != blink_on)
    {
        /* Currently cursor and characters blink at the same rate, but they might not. */
        pThis->last_chr_blink = blink_on;
        pThis->last_cur_blink = blink_on;
        chr_blink_flip = true;
        cur_blink_flip = true;
    }

    for(cy = 0; cy < (height - dscan); cy = cy + (1 << dscan)) {
        d1 = dest;
        src = s1;
        cx_min = width;
        cx_max = -1;
        for(cx = 0; cx < width; cx++) {
            ch_attr = *(uint16_t *)src;
            /* Figure out if character needs redrawing due to blink state change. */
            blink_do_redraw = blink_enabled && chr_blink_flip && (ch_attr & 0x8000);
            if (full_update || ch_attr != (int)*ch_attr_ptr || blink_do_redraw || (src == cursor_ptr && cur_blink_flip)) {
                if (cx < cx_min)
                    cx_min = cx;
                if (cx > cx_max)
                    cx_max = cx;
                if (reset_dirty)
                    *ch_attr_ptr = ch_attr;
#ifdef WORDS_BIGENDIAN
                ch = ch_attr >> 8;
                cattr = ch_attr & 0xff;
#else
                ch = ch_attr & 0xff;
                cattr = ch_attr >> 8;
#endif
                font_ptr = font_base[(cattr >> 3) & 1];
                font_ptr += 32 * 4 * ch;
                bgcol = palette[cattr >> 4];
                fgcol = palette[cattr & 0x0f];

                if (blink_enabled && (cattr & 0x80))
                {
                    bgcol = palette[(cattr >> 4) & 7];
                    if (!blink_on)
                        font_ptr = empty_glyph;
                }

                if (cw != 9) {
                    if (pThis->fRenderVRAM)
                        vga_draw_glyph8(d1, linesize, font_ptr, cheight, fgcol, bgcol, dscan);
                } else {
                    dup9 = 0;
                    if (ch >= 0xb0 && ch <= 0xdf && (pThis->ar[0x10] & 0x04))
                        dup9 = 1;
                    if (pThis->fRenderVRAM)
                        vga_draw_glyph9(d1, linesize, font_ptr, cheight, fgcol, bgcol, dup9);
                }

                /* Underline. Typically turned off by setting it past cell height. */
                if (((cattr & 0x03) == 1) && (uline_pos < cheight))
                {
                    int h;

                    d = d1 + (linesize * uline_pos << dscan);
                    h = 1;

                    if (cw != 9) {
                        if (pThis->fRenderVRAM)
                            vga_draw_glyph8(d, linesize, cursor_glyph, h, fgcol, bgcol, dscan);
                    } else {
                        if (pThis->fRenderVRAM)
                            vga_draw_glyph9(d, linesize, cursor_glyph, h, fgcol, bgcol, 1);
                    }
                }

                /* Cursor. */
                if (src == cursor_ptr &&
                    !(pThis->cr[0x0a] & 0x20)) {
                    int line_start, line_last, h;

                    /* draw the cursor if within the visible period */
                    if (blink_on) {
                        line_start = pThis->cr[0x0a] & 0x1f;
                        line_last = pThis->cr[0x0b] & 0x1f;
                        /* XXX: check that */
                        if (line_last > cheight - 1)
                            line_last = cheight - 1;
                        if (line_last >= line_start && line_start < cheight) {
                            h = line_last - line_start + 1;
                            d = d1 + (linesize * line_start << dscan);
                            if (cw != 9) {
                                if (pThis->fRenderVRAM)
                                    vga_draw_glyph8(d, linesize, cursor_glyph, h, fgcol, bgcol, dscan);
                            } else {
                                if (pThis->fRenderVRAM)
                                    vga_draw_glyph9(d, linesize, cursor_glyph, h, fgcol, bgcol, 1);
                            }
                        }
                    }
                }
            }
            d1 += x_incr;
            src += s_incr;  /* Even in text mode, word/byte mode matters. */
            if (src > (pThisCC->pbVRam + addr_mask))
                src = pThisCC->pbVRam;
            ch_attr_ptr++;
        }
        if (cx_max != -1) {
            /* Keep track of the bounding rectangle for updates. */
            if (cy_start == -1)
                cy_start = cy;
            if (cx_min_upd > cx_min)
                cx_min_upd = cx_min;
            if (cx_max_upd < cx_max)
                cx_max_upd = cx_max;
        } else if (cy_start >= 0) {
            /* Flush updates to display. */
            pDrv->pfnUpdateRect(pDrv, cx_min_upd * cw, cy_start * cheight,
                                (cx_max_upd - cx_min_upd + 1) * cw, (cy - cy_start) * cheight);
            cy_start = -1;
            cx_max_upd = -1;
            cx_min_upd = width;
        }

        dest += linesize * cheight << dscan;
        s1 += line_offset;

        /* Line compare works in text modes, too. */
        /** @todo r=michaln This is inaccurate; text should be rendered line by line
         * and line compare checked after every line. */
        if ((uint32_t)cy == (pThis->line_compare / cheight))
            s1 = pThisCC->pbVRam;

        if (s1 > (pThisCC->pbVRam + addr_mask))
            s1 = s1 - (addr_mask + 1);
    }
    if (cy_start >= 0)
        /* Flush any remaining changes to display. */
        pDrv->pfnUpdateRect(pDrv, cx_min_upd * cw, cy_start * cheight,
                            (cx_max_upd - cx_min_upd + 1) * cw, (cy - cy_start) * cheight);
    return VINF_SUCCESS;
}

enum {
    VGA_DRAW_LINE2,
    VGA_DRAW_LINE2D2,
    VGA_DRAW_LINE4,
    VGA_DRAW_LINE4D2,
    VGA_DRAW_LINE8D2,
    VGA_DRAW_LINE8,
    VGA_DRAW_LINE15,
    VGA_DRAW_LINE16,
    VGA_DRAW_LINE24,
    VGA_DRAW_LINE32,
    VGA_DRAW_LINE_NB
};

static vga_draw_line_func * const vga_draw_line_table[4 * VGA_DRAW_LINE_NB] = {
    vga_draw_line2_8,
    vga_draw_line2_16,
    vga_draw_line2_16,
    vga_draw_line2_32,

    vga_draw_line2d2_8,
    vga_draw_line2d2_16,
    vga_draw_line2d2_16,
    vga_draw_line2d2_32,

    vga_draw_line4_8,
    vga_draw_line4_16,
    vga_draw_line4_16,
    vga_draw_line4_32,

    vga_draw_line4d2_8,
    vga_draw_line4d2_16,
    vga_draw_line4d2_16,
    vga_draw_line4d2_32,

    vga_draw_line8d2_8,
    vga_draw_line8d2_16,
    vga_draw_line8d2_16,
    vga_draw_line8d2_32,

    vga_draw_line8_8,
    vga_draw_line8_16,
    vga_draw_line8_16,
    vga_draw_line8_32,

    vga_draw_line15_8,
    vga_draw_line15_15,
    vga_draw_line15_16,
    vga_draw_line15_32,

    vga_draw_line16_8,
    vga_draw_line16_15,
    vga_draw_line16_16,
    vga_draw_line16_32,

    vga_draw_line24_8,
    vga_draw_line24_15,
    vga_draw_line24_16,
    vga_draw_line24_32,

    vga_draw_line32_8,
    vga_draw_line32_15,
    vga_draw_line32_16,
    vga_draw_line32_32,
};

static int vgaR3GetBpp(PVGASTATE pThis)
{
    int ret;
#ifdef CONFIG_BOCHS_VBE
    if (pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        ret = pThis->vbe_regs[VBE_DISPI_INDEX_BPP];
    } else
#endif
    {
        ret = 0;
    }
    return ret;
}

static void vgaR3GetResolution(PVGASTATE pThis, int *pwidth, int *pheight)
{
    int width, height;
#ifdef CONFIG_BOCHS_VBE
    if (pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        width = pThis->vbe_regs[VBE_DISPI_INDEX_XRES];
        height = RT_MIN(pThis->vbe_regs[VBE_DISPI_INDEX_YRES],
                        pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT]);
    } else
#endif
    {
        width = (pThis->cr[0x01] + 1) * 8;
        height = pThis->cr[0x12] |
            ((pThis->cr[0x07] & 0x02) << 7) |
            ((pThis->cr[0x07] & 0x40) << 3);
        height = (height + 1);
    }
    *pwidth = width;
    *pheight = height;
}


/**
 * Performs the display driver resizing when in graphics mode.
 *
 * This will recalc / update any status data depending on the driver
 * properties (bit depth mostly).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VINF_VGA_RESIZE_IN_PROGRESS if the operation wasn't complete.
 * @param   pThis   Pointer to the shared VGA state.
 * @param   pThisCC Pointer to the ring-3 VGA state.
 * @param   cx      The width.
 * @param   cy      The height.
 * @param   pDrv    The display connector.
 */
static int vgaR3ResizeGraphic(PVGASTATE pThis, PVGASTATER3 pThisCC, int cx, int cy, PDMIDISPLAYCONNECTOR *pDrv)
{
    const unsigned cBits = pThisCC->get_bpp(pThis);

    int rc;
    AssertReturn(cx, VERR_INVALID_PARAMETER);
    AssertReturn(cy, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    if (!pThis->line_offset)
        return VERR_INTERNAL_ERROR;

#if 0 //def VBOX_WITH_VDMA
    /** @todo we get a second resize here when VBVA is on, while we actually should not */
    /* do not do pfnResize in case VBVA is on since all mode changes are performed over VBVA
     * we are checking for VDMA state here to ensure this code works only for WDDM driver,
     * although we should avoid calling pfnResize for XPDM as well, since pfnResize is actually an extra resize
     * event and generally only pfnVBVAxxx calls should be used with HGSMI + VBVA
     *
     * The reason for doing this for WDDM driver only now is to avoid regressions of the current code */
    PVBOXVDMAHOST pVdma = pThisCC->pVdma;
    if (pVdma && vboxVDMAIsEnabled(pVdma))
        rc = VINF_SUCCESS;
    else
#endif
    {
        /* Skip the resize if the values are not valid. */
        if (pThis->start_addr * 4 + pThis->line_offset * cy < pThis->vram_size)
            /* Take into account the programmed start address (in DWORDs) of the visible screen. */
            rc = pDrv->pfnResize(pDrv, cBits, pThisCC->pbVRam + pThis->start_addr * 4, pThis->line_offset, cx, cy);
        else
        {
            /* Change nothing in the VGA state. Lets hope the guest will eventually programm correct values. */
            return VERR_TRY_AGAIN;
        }
    }

    /* last stuff */
    pThis->last_bpp = cBits;
    pThis->last_scr_width = cx;
    pThis->last_scr_height = cy;
    pThis->last_width = cx;
    pThis->last_height = cy;

    if (rc == VINF_VGA_RESIZE_IN_PROGRESS)
        return rc;
    AssertRC(rc);

    /* update palette */
    switch (pDrv->cBits)
    {
        case 32:    pThisCC->rgb_to_pixel = rgb_to_pixel32_dup; break;
        case 16:
        default:    pThisCC->rgb_to_pixel = rgb_to_pixel16_dup; break;
        case 15:    pThisCC->rgb_to_pixel = rgb_to_pixel15_dup; break;
        case 8:     pThisCC->rgb_to_pixel = rgb_to_pixel8_dup;  break;
    }
    if (pThis->shift_control == 0)
        vgaR3UpdatePalette16(pThis, pThisCC);
    else if (pThis->shift_control == 1)
        vgaR3UpdatePalette16(pThis, pThisCC);
    return VINF_SUCCESS;
}

# ifdef VBOX_WITH_VMSVGA

#  if 0 /* unused? */
int vgaR3UpdateDisplay(PVGASTATE pThis, PVGASTATER3 pThisCC, unsigned xStart, unsigned yStart, unsigned cx, unsigned cy, PDMIDISPLAYCONNECTOR *pDrv)
{
    uint32_t v;
    vga_draw_line_func *vga_draw_line;

    if (!pThis->fRenderVRAM)
    {
        pDrv->pfnUpdateRect(pDrv, xStart, yStart, cx, cy);
        return VINF_SUCCESS;
    }
    /** @todo might crash if a blit follows a resolution change very quickly (seen this many times!) */

    if (    pThis->svga.uWidth  == VMSVGA_VAL_UNINITIALIZED
        ||  pThis->svga.uHeight == VMSVGA_VAL_UNINITIALIZED
        ||  pThis->svga.uBpp    == VMSVGA_VAL_UNINITIALIZED)
    {
        /* Intermediate state; skip redraws. */
        AssertFailed();
        return VINF_SUCCESS;
    }

    uint32_t cBits;
    switch (pThis->svga.uBpp) {
    default:
    case 0:
    case 8:
        AssertFailed();
        return VERR_NOT_IMPLEMENTED;
    case 15:
        v = VGA_DRAW_LINE15;
        cBits = 16;
        break;
    case 16:
        v = VGA_DRAW_LINE16;
        cBits = 16;
        break;
    case 24:
        v = VGA_DRAW_LINE24;
        cBits = 24;
        break;
    case 32:
        v = VGA_DRAW_LINE32;
        cBits = 32;
        break;
    }
    vga_draw_line = vga_draw_line_table[v * 4 + vgaR3GetDepthIndex(pDrv->cBits)];

    uint32_t offSrc = (xStart * cBits) / 8 + pThis->svga.cbScanline * yStart;
    uint32_t offDst = (xStart * RT_ALIGN(pDrv->cBits, 8)) / 8 + pDrv->cbScanline * yStart;

    uint8_t       *pbDst = pDrv->pbData     + offDst;
    uint8_t const *pbSrc = pThisCC->pbVRam  + offSrc;

    for (unsigned y = yStart; y < yStart + cy; y++)
    {
        vga_draw_line(pThis, pThisCC, pbDst, pbSrc, cx);

        pbDst += pDrv->cbScanline;
        pbSrc += pThis->svga.cbScanline;
    }
    pDrv->pfnUpdateRect(pDrv, xStart, yStart, cx, cy);

    return VINF_SUCCESS;
}
#  endif

/**
 * graphic modes
 */
static int vmsvgaR3DrawGraphic(PVGASTATE pThis, PVGASTATER3 pThisCC, bool fFullUpdate,
                               bool fFailOnResize, bool reset_dirty, PDMIDISPLAYCONNECTOR *pDrv)
{
    RT_NOREF1(fFailOnResize);

    uint32_t const cx        = pThis->last_scr_width;
    uint32_t const cxDisplay = cx;
    uint32_t const cy        = pThis->last_scr_height;
    uint32_t       cBits     = pThis->last_bpp;

    if (   cx    == VMSVGA_VAL_UNINITIALIZED
        || cx    == 0
        || cy    == VMSVGA_VAL_UNINITIALIZED
        || cy    == 0
        || cBits == VMSVGA_VAL_UNINITIALIZED
        || cBits == 0
        || cx    != pDrv->cx
        || cy    != pDrv->cy
        || cBits != pDrv->cBits
        || (cx * cBits + 7) / 8 > pDrv->cbScanline)
    {
        /* Intermediate state; skip redraws. */
        return VINF_SUCCESS;
    }

    unsigned v;
    switch (cBits)
    {
        case 8:
            /* Note! experimental, not sure if this really works... */
            /** @todo fFullUpdate |= vgaR3UpdatePalette256(pThis); - need fFullUpdate but not
             *        copying anything to last_palette. */
            v = VGA_DRAW_LINE8;
            break;
        case 15:
            v = VGA_DRAW_LINE15;
            cBits = 16;
            break;
        case 16:
            v = VGA_DRAW_LINE16;
            break;
        case 24:
            v = VGA_DRAW_LINE24;
            break;
        case 32:
            v = VGA_DRAW_LINE32;
            break;
        default:
        /*case 0: - Superfluous, checked already in the if above */
            AssertFailed();
            return VERR_NOT_IMPLEMENTED;
    }
    vga_draw_line_func *pfnVgaDrawLine = vga_draw_line_table[v * 4 + vgaR3GetDepthIndex(pDrv->cBits)];

    Assert(!pThisCC->cursor_invalidate);
    Assert(!pThisCC->cursor_draw_line);
    //not used// if (pThisCC->cursor_invalidate)
    //not used//     pThisCC->cursor_invalidate(pThis);

    uint8_t    *pbDst          = pDrv->pbData;
    uint32_t    cbDstScanline  = pDrv->cbScanline;
    uint32_t    offSrcStart    = 0;  /* always start at the beginning of the framebuffer */
    uint32_t    cbScanline     = (cx * cBits + 7) / 8;   /* The visible width of a scanline. */
    uint32_t    yUpdateRectTop = UINT32_MAX;
    uint32_t    offPageMin     = UINT32_MAX;
    int32_t     offPageMax     = -1;
    uint32_t    y;
    for (y = 0; y < cy; y++)
    {
        uint32_t offSrcLine = offSrcStart + y * cbScanline;
        uint32_t offPage0   = offSrcLine & ~(uint32_t)GUEST_PAGE_OFFSET_MASK;
        uint32_t offPage1   = (offSrcLine + cbScanline - 1) & ~(uint32_t)GUEST_PAGE_OFFSET_MASK;
        /** @todo r=klaus this assumes that a line is fully covered by 3 pages,
         * irrespective of alignment. Not guaranteed for high res modes, i.e.
         * anything wider than 2050 pixels @32bpp. Need to check all pages
         * between the first and last one. */
        bool     fUpdate    = fFullUpdate || vgaR3IsDirty(pThis, offPage0) || vgaR3IsDirty(pThis, offPage1);
        if (offPage1 - offPage0 > GUEST_PAGE_SIZE)
            /* if wide line, can use another page */
            fUpdate |= vgaR3IsDirty(pThis, offPage0 + GUEST_PAGE_SIZE);
        /* explicit invalidation for the hardware cursor */
        fUpdate |= (pThis->invalidated_y_table[y >> 5] >> (y & 0x1f)) & 1;
        if (fUpdate)
        {
            if (yUpdateRectTop == UINT32_MAX)
                yUpdateRectTop = y;
            if (offPage0 < offPageMin)
                offPageMin = offPage0;
            if ((int32_t)offPage1 > offPageMax)
                offPageMax = offPage1;
            if (pThis->fRenderVRAM)
                pfnVgaDrawLine(pThis, pThisCC, pbDst, pThisCC->pbVRam + offSrcLine, cx);
            //not used// if (pThisCC->cursor_draw_line)
            //not used//     pThisCC->cursor_draw_line(pThis, pbDst, y);
        }
        else if (yUpdateRectTop != UINT32_MAX)
        {
            /* flush to display */
            Log(("Flush to display (%d,%d)(%d,%d)\n", 0, yUpdateRectTop, cxDisplay, y - yUpdateRectTop));
            pDrv->pfnUpdateRect(pDrv, 0, yUpdateRectTop, cxDisplay, y - yUpdateRectTop);
            yUpdateRectTop = UINT32_MAX;
        }
        pbDst += cbDstScanline;
    }
    if (yUpdateRectTop != UINT32_MAX)
    {
        /* flush to display */
        Log(("Flush to display (%d,%d)(%d,%d)\n", 0, yUpdateRectTop, cxDisplay, y - yUpdateRectTop));
        pDrv->pfnUpdateRect(pDrv, 0, yUpdateRectTop, cxDisplay, y - yUpdateRectTop);
    }

    /* reset modified pages */
    if (offPageMax != -1 && reset_dirty)
        vgaR3ResetDirty(pThis, offPageMin, offPageMax + GUEST_PAGE_SIZE);
    memset(pThis->invalidated_y_table, 0, ((cy + 31) >> 5) * 4);

    return VINF_SUCCESS;
}

# endif /* VBOX_WITH_VMSVGA */

/**
 * graphic modes
 */
static int vgaR3DrawGraphic(PVGASTATE pThis, PVGASTATER3 pThisCC, bool full_update, bool fFailOnResize, bool reset_dirty,
                            PDMIDISPLAYCONNECTOR *pDrv)
{
    int y1, y2, y, page_min, page_max, linesize, y_start, double_scan;
    int width, height, shift_control, line_offset, page0, page1, bwidth, bits;
    int disp_width, multi_run;
    uint8_t *d;
    uint32_t v, addr1, addr;
    vga_draw_line_func *pfnVgaDrawLine;

    bool offsets_changed = vgaR3UpdateBasicParams(pThis, pThisCC);

    full_update |= offsets_changed;

    pThisCC->get_resolution(pThis, &width, &height);
    disp_width = width;

    shift_control = (pThis->gr[0x05] >> 5) & 3;
    double_scan = (pThis->cr[0x09] >> 7);
    multi_run = double_scan;
    if (shift_control != pThis->shift_control ||
        double_scan != pThis->double_scan) {
        full_update = true;
        pThis->shift_control = shift_control;
        pThis->double_scan = double_scan;
    }

    if (shift_control == 0) {
        full_update |= vgaR3UpdatePalette16(pThis, pThisCC);
        if (pThis->sr[0x01] & 8) {
            v = VGA_DRAW_LINE4D2;
            disp_width <<= 1;
        } else {
            v = VGA_DRAW_LINE4;
        }
        bits = 4;
    } else if (shift_control == 1) {
        full_update |= vgaR3UpdatePalette16(pThis, pThisCC);
        if (pThis->sr[0x01] & 8) {
            v = VGA_DRAW_LINE2D2;
            disp_width <<= 1;
        } else {
            v = VGA_DRAW_LINE2;
        }
        bits = 4;
    } else {
        switch(pThisCC->get_bpp(pThis)) {
        default:
        case 0:
            full_update |= vgaR3UpdatePalette256(pThis, pThisCC);
            v = VGA_DRAW_LINE8D2;
            bits = 4;
            break;
        case 8:
            full_update |= vgaR3UpdatePalette256(pThis, pThisCC);
            v = VGA_DRAW_LINE8;
            bits = 8;
            break;
        case 15:
            v = VGA_DRAW_LINE15;
            bits = 16;
            break;
        case 16:
            v = VGA_DRAW_LINE16;
            bits = 16;
            break;
        case 24:
            v = VGA_DRAW_LINE24;
            bits = 24;
            break;
        case 32:
            v = VGA_DRAW_LINE32;
            bits = 32;
            break;
        }
    }
    if (    disp_width     != (int)pThis->last_width
        ||  height         != (int)pThis->last_height
        ||  pThisCC->get_bpp(pThis)  != (int)pThis->last_bpp
        || (offsets_changed && !pThis->fRenderVRAM)
        || disp_width      != (int)pDrv->cx
        || height          != (int)pDrv->cy)
    {
        if (fFailOnResize)
        {
            /* The caller does not want to call the pfnResize. */
            return VERR_TRY_AGAIN;
        }
        int rc = vgaR3ResizeGraphic(pThis, pThisCC, disp_width, height, pDrv);
        if (rc != VINF_SUCCESS)  /* Return any rc, particularly VINF_VGA_RESIZE_IN_PROGRESS, to the caller. */
            return rc;
        full_update = true;
    }

    if (pThis->fRenderVRAM)
    {
        /* Do not update the destination buffer if it is not big enough.
         * Can happen if the resize request was ignored by the driver.
         * Compare with 'disp_width', because it is what the framebuffer has been resized to.
         */
        if (   pDrv->cx != (uint32_t)disp_width
            || pDrv->cy != (uint32_t)height)
        {
            LogRel(("Framebuffer mismatch: vga %dx%d, drv %dx%d!!!\n",
                    disp_width, height,
                    pDrv->cx, pDrv->cy));
            return VINF_SUCCESS;
        }
    }

    pfnVgaDrawLine = vga_draw_line_table[v * 4 + vgaR3GetDepthIndex(pDrv->cBits)];

    if (pThisCC->cursor_invalidate)
        pThisCC->cursor_invalidate(pThis);

    line_offset = pThis->line_offset;
#if 0
    Log(("w=%d h=%d v=%d line_offset=%d cr[0x09]=0x%02x cr[0x17]=0x%02x linecmp=%d sr[0x01]=0x%02x\n",
           width, height, v, line_offset, pThis->cr[9], pThis->cr[0x17], pThis->line_compare, pThis->sr[0x01]));
#endif
    addr1 = (pThis->start_addr * 4);
    bwidth = (width * bits + 7) / 8;    /* The visible width of a scanline. */
    y_start = -1;
    page_min = 0x7fffffff;
    page_max = -1;
    d = pDrv->pbData;
    linesize = pDrv->cbScanline;

    if (!(pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED))
        pThis->vga_addr_mask = 0x3ffff;
    else
        pThis->vga_addr_mask = UINT32_MAX;

    y1 = 0;
    y2 = pThis->cr[0x09] & 0x1F;    /* starting row scan count */
    for(y = 0; y < height; y++) {
        addr = addr1;
        /* CGA/MDA compatibility. Note that these addresses are all
         * shifted left by two compared to VGA specs.
         */
        if (!(pThis->cr[0x17] & 1)) {
            addr = (addr & ~(1 << 15)) | ((y1 & 1) << 15);
        }
        if (!(pThis->cr[0x17] & 2)) {
            addr = (addr & ~(1 << 16)) | ((y1 & 2) << 15);
        }
        addr &= pThis->vga_addr_mask;
        page0 = addr & ~(uint32_t)GUEST_PAGE_OFFSET_MASK;
        page1 = (addr + bwidth - 1) & ~(uint32_t)GUEST_PAGE_OFFSET_MASK;
        /** @todo r=klaus this assumes that a line is fully covered by 3 pages,
         * irrespective of alignment. Not guaranteed for high res modes, i.e.
         * anything wider than 2050 pixels @32bpp. Need to check all pages
         * between the first and last one. */
        bool update = full_update || vgaR3IsDirty(pThis, page0) || vgaR3IsDirty(pThis, page1);
        if (page1 - page0 > GUEST_PAGE_SIZE) {
            /* if wide line, can use another page */
            update |= vgaR3IsDirty(pThis, page0 + GUEST_PAGE_SIZE);
        }
        /* explicit invalidation for the hardware cursor */
        update |= (pThis->invalidated_y_table[y >> 5] >> (y & 0x1f)) & 1;
        if (update) {
            if (y_start < 0)
                y_start = y;
            if (page0 < page_min)
                page_min = page0;
            if (page1 > page_max)
                page_max = page1;
            if (pThis->fRenderVRAM)
                pfnVgaDrawLine(pThis, pThisCC, d, pThisCC->pbVRam + addr, width);
            if (pThisCC->cursor_draw_line)
                pThisCC->cursor_draw_line(pThis, d, y);
        } else {
            if (y_start >= 0) {
                /* flush to display */
                pDrv->pfnUpdateRect(pDrv, 0, y_start, disp_width, y - y_start);
                y_start = -1;
            }
        }
        if (!multi_run) {
            y1++;
            multi_run = double_scan;

            if (y2 == 0) {
                y2 = pThis->cr[0x09] & 0x1F;
                addr1 += line_offset;
            } else {
                --y2;
            }
        } else {
            multi_run--;
        }
        /* line compare acts on the displayed lines */
        if ((uint32_t)y == pThis->line_compare)
            addr1 = 0;
        d += linesize;
    }
    if (y_start >= 0) {
        /* flush to display */
        pDrv->pfnUpdateRect(pDrv, 0, y_start, disp_width, y - y_start);
    }
    /* reset modified pages */
    if (page_max != -1 && reset_dirty) {
        vgaR3ResetDirty(pThis, page_min, page_max + GUEST_PAGE_SIZE);
    }
    memset(pThis->invalidated_y_table, 0, ((height + 31) >> 5) * 4);
    return VINF_SUCCESS;
}

/**
 * blanked modes
 */
static int vgaR3DrawBlank(PVGASTATE pThis, PVGASTATER3 pThisCC, bool full_update,
                          bool fFailOnResize, bool reset_dirty, PDMIDISPLAYCONNECTOR *pDrv)
{
    int i, w, val;
    uint8_t *d;
    uint32_t cbScanline = pDrv->cbScanline;
    uint32_t page_min, page_max;

    if (pThis->last_width != 0)
    {
        if (fFailOnResize)
        {
            /* The caller does not want to call the pfnResize. */
            return VERR_TRY_AGAIN;
        }
        pThis->last_width = 0;
        pThis->last_height = 0;
        /* For blanking signal width=0, height=0, bpp=0 and cbLine=0 here.
         * There is no screen content, which distinguishes it from text mode. */
        pDrv->pfnResize(pDrv, 0, NULL, 0, 0, 0);
    }
    /* reset modified pages, i.e. everything */
    if (reset_dirty && pThis->last_scr_height > 0)
    {
        page_min = (pThis->start_addr * 4) & ~(uint32_t)GUEST_PAGE_OFFSET_MASK;
        /* round up page_max by one page, as otherwise this can be -GUEST_PAGE_SIZE,
         * which causes assertion trouble in vgaR3ResetDirty. */
        page_max = (pThis->start_addr * 4 + pThis->line_offset * pThis->last_scr_height - 1 + GUEST_PAGE_SIZE)
                 & ~(uint32_t)GUEST_PAGE_OFFSET_MASK;
        vgaR3ResetDirty(pThis, page_min, page_max + GUEST_PAGE_SIZE);
    }
    if (pDrv->pbData == pThisCC->pbVRam) /* Do not clear the VRAM itself. */
        return VINF_SUCCESS;
    if (!full_update)
        return VINF_SUCCESS;
    if (pThis->last_scr_width <= 0 || pThis->last_scr_height <= 0)
        return VINF_SUCCESS;
    if (pDrv->cBits == 8)
        val = pThisCC->rgb_to_pixel(0, 0, 0);
    else
        val = 0;
    w = pThis->last_scr_width * ((pDrv->cBits + 7) >> 3);
    d = pDrv->pbData;
    if (pThis->fRenderVRAM)
    {
        for(i = 0; i < (int)pThis->last_scr_height; i++) {
            memset(d, val, w);
            d += cbScanline;
        }
    }
    pDrv->pfnUpdateRect(pDrv, 0, 0, pThis->last_scr_width, pThis->last_scr_height);
    return VINF_SUCCESS;
}


#define GMODE_TEXT      0
#define GMODE_GRAPH     1
#define GMODE_BLANK     2
#ifdef VBOX_WITH_VMSVGA
#define GMODE_SVGA      3
#endif

/**
 * Worker for vgaR3PortUpdateDisplay(), vgaR3UpdateDisplayAllInternal() and
 * vgaR3PortTakeScreenshot().
 */
static int vgaR3UpdateDisplay(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATER3 pThisCC, bool fUpdateAll,
                              bool fFailOnResize, bool reset_dirty, PDMIDISPLAYCONNECTOR *pDrv, int32_t *pcur_graphic_mode)
{
    int rc = VINF_SUCCESS;
    int graphic_mode;

    if (pDrv->cBits == 0) {
        /* nothing to do */
    } else {
        switch(pDrv->cBits) {
        case 8:
            pThisCC->rgb_to_pixel = rgb_to_pixel8_dup;
            break;
        case 15:
            pThisCC->rgb_to_pixel = rgb_to_pixel15_dup;
            break;
        default:
        case 16:
            pThisCC->rgb_to_pixel = rgb_to_pixel16_dup;
            break;
        case 32:
            pThisCC->rgb_to_pixel = rgb_to_pixel32_dup;
            break;
        }

#ifdef VBOX_WITH_VMSVGA
        if (pThis->svga.fEnabled) {
            graphic_mode = GMODE_SVGA;
        }
        else
#endif
        if (!(pThis->ar_index & 0x20) || (pThis->sr[0x01] & 0x20)) {
            graphic_mode = GMODE_BLANK;
        } else {
            graphic_mode = pThis->gr[6] & 1 ? GMODE_GRAPH : GMODE_TEXT;
        }
        bool full_update = fUpdateAll || graphic_mode != *pcur_graphic_mode;
        if (full_update) {
            *pcur_graphic_mode = graphic_mode;
        }
        switch(graphic_mode) {
        case GMODE_TEXT:
            rc = vgaR3DrawText(pDevIns, pThis, pThisCC, full_update, fFailOnResize, reset_dirty, pDrv);
            break;
        case GMODE_GRAPH:
            rc = vgaR3DrawGraphic(pThis, pThisCC, full_update, fFailOnResize, reset_dirty, pDrv);
            break;
#ifdef VBOX_WITH_VMSVGA
        case GMODE_SVGA:
            rc = vmsvgaR3DrawGraphic(pThis, pThisCC, full_update, fFailOnResize, reset_dirty, pDrv);
            break;
#endif
        case GMODE_BLANK:
        default:
            rc = vgaR3DrawBlank(pThis, pThisCC, full_update, fFailOnResize, reset_dirty, pDrv);
            break;
        }
    }
    return rc;
}

/**
 * Worker for vgaR3SaveExec().
 */
static void vga_save(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PVGASTATE pThis)
{
    int i;

    pHlp->pfnSSMPutU32(pSSM, pThis->latch);
    pHlp->pfnSSMPutU8(pSSM, pThis->sr_index);
    pHlp->pfnSSMPutMem(pSSM, pThis->sr, 8);
    pHlp->pfnSSMPutU8(pSSM, pThis->gr_index);
    pHlp->pfnSSMPutMem(pSSM, pThis->gr, 16);
    pHlp->pfnSSMPutU8(pSSM, pThis->ar_index);
    pHlp->pfnSSMPutMem(pSSM, pThis->ar, 21);
    pHlp->pfnSSMPutU32(pSSM, pThis->ar_flip_flop);
    pHlp->pfnSSMPutU8(pSSM, pThis->cr_index);
    pHlp->pfnSSMPutMem(pSSM, pThis->cr, 256);
    pHlp->pfnSSMPutU8(pSSM, pThis->msr);
    pHlp->pfnSSMPutU8(pSSM, pThis->fcr);
    pHlp->pfnSSMPutU8(pSSM, pThis->st00);
    pHlp->pfnSSMPutU8(pSSM, pThis->st01);

    pHlp->pfnSSMPutU8(pSSM, pThis->dac_state);
    pHlp->pfnSSMPutU8(pSSM, pThis->dac_sub_index);
    pHlp->pfnSSMPutU8(pSSM, pThis->dac_read_index);
    pHlp->pfnSSMPutU8(pSSM, pThis->dac_write_index);
    pHlp->pfnSSMPutMem(pSSM, pThis->dac_cache, 3);
    pHlp->pfnSSMPutMem(pSSM, pThis->palette, 768);

    pHlp->pfnSSMPutU32(pSSM, pThis->bank_offset);
#ifdef CONFIG_BOCHS_VBE
    AssertCompile(RT_ELEMENTS(pThis->vbe_regs) < 256);
    pHlp->pfnSSMPutU8(pSSM, (uint8_t)RT_ELEMENTS(pThis->vbe_regs));
    pHlp->pfnSSMPutU16(pSSM, pThis->vbe_index);
    for(i = 0; i < (int)RT_ELEMENTS(pThis->vbe_regs); i++)
        pHlp->pfnSSMPutU16(pSSM, pThis->vbe_regs[i]);
    pHlp->pfnSSMPutU32(pSSM, pThis->vbe_start_addr);
    pHlp->pfnSSMPutU32(pSSM, pThis->vbe_line_offset);
#else
    pHlp->pfnSSMPutU8(pSSM, 0);
#endif
}


/**
 * Worker for vgaR3LoadExec().
 */
static int vga_load(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PVGASTATE pThis, int version_id)
{
    int is_vbe, i;
    uint32_t u32Dummy;
    uint8_t u8;

    pHlp->pfnSSMGetU32(pSSM, &pThis->latch);
    pHlp->pfnSSMGetU8(pSSM, &pThis->sr_index);
    pHlp->pfnSSMGetMem(pSSM, pThis->sr, 8);
    pHlp->pfnSSMGetU8(pSSM, &pThis->gr_index);
    pHlp->pfnSSMGetMem(pSSM, pThis->gr, 16);
    pHlp->pfnSSMGetU8(pSSM, &pThis->ar_index);
    pHlp->pfnSSMGetMem(pSSM, pThis->ar, 21);
    pHlp->pfnSSMGetS32(pSSM, &pThis->ar_flip_flop);
    pHlp->pfnSSMGetU8(pSSM, &pThis->cr_index);
    pHlp->pfnSSMGetMem(pSSM, pThis->cr, 256);
    pHlp->pfnSSMGetU8(pSSM, &pThis->msr);
    pHlp->pfnSSMGetU8(pSSM, &pThis->fcr);
    pHlp->pfnSSMGetU8(pSSM, &pThis->st00);
    pHlp->pfnSSMGetU8(pSSM, &pThis->st01);

    pHlp->pfnSSMGetU8(pSSM, &pThis->dac_state);
    pHlp->pfnSSMGetU8(pSSM, &pThis->dac_sub_index);
    pHlp->pfnSSMGetU8(pSSM, &pThis->dac_read_index);
    pHlp->pfnSSMGetU8(pSSM, &pThis->dac_write_index);
    pHlp->pfnSSMGetMem(pSSM, pThis->dac_cache, 3);
    pHlp->pfnSSMGetMem(pSSM, pThis->palette, 768);

    pHlp->pfnSSMGetS32(pSSM, &pThis->bank_offset);
    pHlp->pfnSSMGetU8(pSSM, &u8);
    is_vbe = !!u8;
#ifdef CONFIG_BOCHS_VBE
    if (!is_vbe)
    {
        Log(("vga_load: !is_vbe !!\n"));
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }

    if (u8 == 1)
        u8 = VBE_DISPI_INDEX_NB_SAVED; /* Used to save so many registers. */
    if (u8 > RT_ELEMENTS(pThis->vbe_regs))
    {
        Log(("vga_load: saved %d, expected %d!!\n", u8, RT_ELEMENTS(pThis->vbe_regs)));
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }

    pHlp->pfnSSMGetU16(pSSM, &pThis->vbe_index);
    for(i = 0; i < (int)u8; i++)
        pHlp->pfnSSMGetU16(pSSM, &pThis->vbe_regs[i]);
    if (version_id <= VGA_SAVEDSTATE_VERSION_INV_VHEIGHT)
        recalculate_data(pThis);    /* <- re-calculate the pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT] since it might be invalid */
    pHlp->pfnSSMGetU32(pSSM, &pThis->vbe_start_addr);
    pHlp->pfnSSMGetU32(pSSM, &pThis->vbe_line_offset);
    if (version_id < 2)
        pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
    pThis->vbe_bank_max = (pThis->vram_size >> 16) - 1;
#else
    if (is_vbe)
    {
        Log(("vga_load: is_vbe !!\n"));
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }
#endif

    /* force refresh */
    pThis->graphic_mode = -1;
    return 0;
}


/**
 * Worker for vgaR3Construct().
 */
static void vgaR3InitExpand(void)
{
    int i, j, v, b;

    for(i = 0;i < 256; i++) {
        v = 0;
        for(j = 0; j < 8; j++) {
            v |= ((i >> j) & 1) << (j * 4);
        }
        expand4[i] = v;

        v = 0;
        for(j = 0; j < 4; j++) {
            v |= ((i >> (2 * j)) & 3) << (j * 4);
        }
        expand2[i] = v;
    }
    for(i = 0; i < 16; i++) {
        v = 0;
        for(j = 0; j < 4; j++) {
            b = ((i >> j) & 1);
            v |= b << (2 * j);
            v |= b << (2 * j + 1);
        }
        expand4to8[i] = v;
    }
}

#endif /* IN_RING3 */



/* -=-=-=-=-=- all contexts -=-=-=-=-=- */

#define VGA_IOPORT_WRITE_PLACEHOLDER(a_uPort, a_cPorts) do {\
        PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE); \
        Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo))); \
        AssertCompile(RT_IS_POWER_OF_TWO(a_cPorts)); \
        Assert((unsigned)offPort - (unsigned)(a_uPort) < (unsigned)(a_cPorts)); \
        NOREF(pvUser); \
        if (cb == 1) \
            vga_ioport_write(pDevIns, pThis, offPort, u32); \
        else if (cb == 2) \
        { \
            vga_ioport_write(pDevIns, pThis, offPort, u32 & 0xff); \
            vga_ioport_write(pDevIns, pThis, offPort + 1, u32 >> 8); \
        } \
        return VINF_SUCCESS; \
    } while (0)

#define VGA_IOPORT_READ_PLACEHOLDER(a_uPort, a_cPorts) do {\
        PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE); \
        Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo))); \
        AssertCompile(RT_IS_POWER_OF_TWO(a_cPorts)); \
        Assert((unsigned)offPort - (unsigned)(a_uPort) < (unsigned)(a_cPorts)); \
        NOREF(pvUser); \
        if (cb == 1) \
            *pu32 = vga_ioport_read(pDevIns, pThis, offPort); \
        else if (cb == 2) \
        { \
            uint32_t u32 = vga_ioport_read(pDevIns, pThis, offPort); \
            u32 |= vga_ioport_read(pDevIns, pThis, offPort + 1) << 8; \
            *pu32 = u32; \
        } \
        else \
            return VERR_IOM_IOPORT_UNUSED; \
        return VINF_SUCCESS; \
    } while (0)

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3c0-0x3c1 Attribute Controller.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortArWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3c0, 2);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3c0-0x3c1 Attribute Controller.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortArRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    VGA_IOPORT_READ_PLACEHOLDER(0x3c0, 2);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3c2 Miscellaneous Register.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortMsrWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3c2, 1);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3c2 Status register 0.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortSt00Read(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    VGA_IOPORT_READ_PLACEHOLDER(0x3c2, 1);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3c3 Unused.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortUnusedWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3c3, 1);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3c3 Unused.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortUnusedRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    VGA_IOPORT_READ_PLACEHOLDER(0x3c3, 1);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3c4-0x3c5 Sequencer.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortSrWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3c4, 2);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3c4-0x3c5 Sequencer.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortSrRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    VGA_IOPORT_READ_PLACEHOLDER(0x3c4, 2);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3c6-0x3c9 DAC.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortDacWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3c6, 4);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3c6-0x3c9 DAC.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortDacRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    VGA_IOPORT_READ_PLACEHOLDER(0x3c6, 4);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3ca-0x3cd Graphics Position?}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortPosWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3ca, 4);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3ca-0x3cd Graphics Position?}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortPosRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    VGA_IOPORT_READ_PLACEHOLDER(0x3ca, 4);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3ce-0x3cf Graphics Controller.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortGrWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3ce, 2);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3ca-0x3cf Graphics Controller.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortGrRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    VGA_IOPORT_READ_PLACEHOLDER(0x3ce, 2);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3b4-0x3b5 MDA CRT control.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortMdaCrtWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    /** @todo do vga_ioport_invalid here   */
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3b4, 2);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3b4-0x3b5 MDA CRT control.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortMdaCrtRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    /** @todo do vga_ioport_invalid here */
    VGA_IOPORT_READ_PLACEHOLDER(0x3b4, 2);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3ba MDA feature/status.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortMdaFcrWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    /** @todo do vga_ioport_invalid here   */
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3ba, 1);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3ba MDA feature/status.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortMdaStRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    /** @todo do vga_ioport_invalid here */
    VGA_IOPORT_READ_PLACEHOLDER(0x3ba, 1);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3d4-0x3d5 CGA CRT control.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortCgaCrtWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    /** @todo do vga_ioport_invalid here   */
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3d4, 2);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3d4-0x3d5 CGA CRT control.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortCgaCrtRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    /** @todo do vga_ioport_invalid here */
    VGA_IOPORT_READ_PLACEHOLDER(0x3d4, 2);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,0x3da CGA feature/status.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortCgaFcrWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    /** @todo do vga_ioport_invalid here   */
    VGA_IOPORT_WRITE_PLACEHOLDER(0x3da, 1);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,0x3da CGA feature/status.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortCgaStRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    /** @todo do vga_ioport_invalid here */
    VGA_IOPORT_READ_PLACEHOLDER(0x3da, 1);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,VBE Data Port OUT handler (0x1ce).}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vgaIoPortWriteVbeData(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

    NOREF(pvUser);

#ifndef IN_RING3
    /*
     * This has to be done on the host in order to execute the connector callbacks.
     */
    if (    pThis->vbe_index == VBE_DISPI_INDEX_ENABLE
        ||  pThis->vbe_index == VBE_DISPI_INDEX_VBOX_VIDEO)
    {
        Log(("vgaIoPortWriteVbeData: VBE_DISPI_INDEX_ENABLE - Switching to host...\n"));
        return VINF_IOM_R3_IOPORT_WRITE;
    }
#endif
#ifdef VBE_BYTEWISE_IO
    if (cb == 1)
    {
        if (!pThis->fWriteVBEData)
        {
            if (    (pThis->vbe_index == VBE_DISPI_INDEX_ENABLE)
                &&  (u32 & VBE_DISPI_ENABLED))
            {
                pThis->fWriteVBEData = false;
                return vbe_ioport_write_data(pDevIns, pThis, pThisCC, offPort, u32 & 0xFF);
            }

            pThis->cbWriteVBEData = u32 & 0xFF;
            pThis->fWriteVBEData = true;
            return VINF_SUCCESS;
        }

        u32 = (pThis->cbWriteVBEData << 8) | (u32 & 0xFF);
        pThis->fWriteVBEData = false;
        cb = 2;
    }
#endif
    if (cb == 2 || cb == 4)
        return vbe_ioport_write_data(pDevIns, pThis, pThisCC, offPort, u32);
    ASSERT_GUEST_MSG_FAILED(("vgaIoPortWriteVbeData: offPort=%#x cb=%d u32=%#x\n", offPort, cb, u32));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,VBE Index Port OUT handler (0x1ce).}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vgaIoPortWriteVbeIndex(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE); NOREF(pvUser);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

#ifdef VBE_BYTEWISE_IO
    if (cb == 1)
    {
        if (!pThis->fWriteVBEIndex)
        {
            pThis->cbWriteVBEIndex = u32 & 0x00FF;
            pThis->fWriteVBEIndex = true;
            return VINF_SUCCESS;
        }
        pThis->fWriteVBEIndex = false;
        vbe_ioport_write_index(pThis, offPort, (pThis->cbWriteVBEIndex << 8) | (u32 & 0x00FF));
        return VINF_SUCCESS;
    }
#endif

    if (cb == 2)
        vbe_ioport_write_index(pThis, offPort, u32);
    else
        ASSERT_GUEST_MSG_FAILED(("vgaIoPortWriteVbeIndex: offPort=%#x cb=%d u32=%#x\n", offPort, cb, u32));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,VBE Data Port IN handler (0x1cf).}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vgaIoPortReadVbeData(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE); NOREF(pvUser);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

#ifdef VBE_BYTEWISE_IO
    if (cb == 1)
    {
        if (!pThis->fReadVBEData)
        {
            *pu32 = (vbe_ioport_read_data(pThis, offPort) >> 8) & 0xFF;
            pThis->fReadVBEData = true;
            return VINF_SUCCESS;
        }
        *pu32 = vbe_ioport_read_data(pThis, offPort) & 0xFF;
        pThis->fReadVBEData = false;
        return VINF_SUCCESS;
    }
#endif
    if (cb == 2)
    {
        *pu32 = vbe_ioport_read_data(pThis, offPort);
        return VINF_SUCCESS;
    }
    if (cb == 4)
    {
        if (pThis->vbe_regs[VBE_DISPI_INDEX_ID] == VBE_DISPI_ID_CFG)
            *pu32 = vbe_ioport_read_data(pThis, offPort); /* New interface. */
        else
            *pu32 = pThis->vram_size; /* Quick hack for getting the vram size. */
        return VINF_SUCCESS;
    }
    ASSERT_GUEST_MSG_FAILED(("vgaIoPortReadVbeData: offPort=%#x cb=%d\n", offPort, cb));
    return VERR_IOM_IOPORT_UNUSED;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,VBE Index Port IN handler (0x1cf).}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vgaIoPortReadVbeIndex(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    NOREF(pvUser);
    PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

#ifdef VBE_BYTEWISE_IO
    if (cb == 1)
    {
        if (!pThis->fReadVBEIndex)
        {
            *pu32 = (vbe_ioport_read_index(pThis, offPort) >> 8) & 0xFF;
            pThis->fReadVBEIndex = true;
            return VINF_SUCCESS;
        }
        *pu32 = vbe_ioport_read_index(pThis, offPort) & 0xFF;
        pThis->fReadVBEIndex = false;
        return VINF_SUCCESS;
    }
#endif
    if (cb == 2)
    {
        *pu32 = vbe_ioport_read_index(pThis, offPort);
        return VINF_SUCCESS;
    }
    ASSERT_GUEST_MSG_FAILED(("vgaIoPortReadVbeIndex: offPort=%#x cb=%d\n", offPort, cb));
    return VERR_IOM_IOPORT_UNUSED;
}

#if defined(VBOX_WITH_HGSMI) && defined(IN_RING3)

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,HGSMI OUT handler.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vgaR3IOPortHgsmiWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    LogFlowFunc(("offPort=0x%x u32=0x%x cb=%u\n", offPort, u32, cb));

    NOREF(pvUser);

    if (cb == 4)
    {
        switch (offPort)
        {
            case VGA_PORT_HGSMI_HOST: /* Host */
            {
# if defined(VBOX_WITH_VDMA) || defined(VBOX_WITH_WDDM)
                if (u32 == HGSMIOFFSET_VOID)
                {
                    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSectIRQ, VERR_SEM_BUSY);
                    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSectIRQ, rcLock);

                    if (pThis->fu32PendingGuestFlags == 0)
                    {
                        PDMDevHlpPCISetIrqNoWait(pDevIns, 0, PDM_IRQ_LEVEL_LOW);
                        HGSMIClearHostGuestFlags(pThisCC->pHGSMI,
                                                 HGSMIHOSTFLAGS_IRQ
                                                 | HGSMIHOSTFLAGS_VSYNC
                                                 | HGSMIHOSTFLAGS_HOTPLUG
                                                 | HGSMIHOSTFLAGS_CURSOR_CAPABILITIES);
                    }
                    else
                    {
                        HGSMISetHostGuestFlags(pThisCC->pHGSMI, HGSMIHOSTFLAGS_IRQ | pThis->fu32PendingGuestFlags);
                        pThis->fu32PendingGuestFlags = 0;
                        /* Keep the IRQ unchanged. */
                    }

                    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSectIRQ);
                }
                else
# endif
                {
                    HGSMIHostWrite(pThisCC->pHGSMI, u32);
                }
                break;
            }

            case VGA_PORT_HGSMI_GUEST: /* Guest */
                HGSMIGuestWrite(pThisCC->pHGSMI, u32);
                break;

            default:
# ifdef DEBUG_sunlover
                AssertMsgFailed(("vgaR3IOPortHgsmiWrite: offPort=%#x cb=%d u32=%#x\n", offPort, cb, u32));
# endif
                break;
        }
    }
    else
    {
        /** @todo r=bird: According to Ralf Brown, one and two byte accesses to the
         *        0x3b0-0x3b1 and 0x3b2-0x3b3 I/O port pairs should work the same as
         *        0x3b4-0x3b5 (MDA CRT control). */
        Log(("vgaR3IOPortHgsmiWrite: offPort=%#x cb=%d u32=%#x - possible valid MDA CRT access\n", offPort, cb, u32));
# ifdef DEBUG_sunlover
        AssertMsgFailed(("vgaR3IOPortHgsmiWrite: offPort=%#x cb=%d u32=%#x\n", offPort, cb, u32));
# endif
        STAM_REL_COUNTER_INC(&pThis->StatHgsmiMdaCgaAccesses);
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,HGSMI IN handler.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vgaR3IOPortHgmsiRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    LogFlowFunc(("offPort=0x%x cb=%d\n", offPort, cb));

    NOREF(pvUser);

    VBOXSTRICTRC rc = VINF_SUCCESS;
    if (cb == 4)
    {
        switch (offPort)
        {
            case VGA_PORT_HGSMI_HOST: /* Host */
                *pu32 = HGSMIHostRead(pThisCC->pHGSMI);
                break;
            case VGA_PORT_HGSMI_GUEST: /* Guest */
                *pu32 = HGSMIGuestRead(pThisCC->pHGSMI);
                break;
            default:
                rc = VERR_IOM_IOPORT_UNUSED;
                break;
        }
    }
    else
    {
        /** @todo r=bird: According to Ralf Brown, one and two byte accesses to the
         *        0x3b0-0x3b1 and 0x3b2-0x3b3 I/O port pairs should work the same as
         *        0x3b4-0x3b5 (MDA CRT control). */
        Log(("vgaR3IOPortHgmsiRead: offPort=%#x cb=%d - possible valid MDA CRT access\n", offPort, cb));
        STAM_REL_COUNTER_INC(&pThis->StatHgsmiMdaCgaAccesses);
        rc = VERR_IOM_IOPORT_UNUSED;
    }

    return rc;
}

#endif /* VBOX_WITH_HGSMI && IN_RING3*/




/* -=-=-=-=-=- All Contexts -=-=-=-=-=- */

/**
 * @internal. For use inside VGAGCMemoryFillWrite only.
 * Macro for apply logical operation and bit mask.
 */
#define APPLY_LOGICAL_AND_MASK(pThis, val, bit_mask) \
    /* apply logical operation */ \
    switch (pThis->gr[3] >> 3)\
    { \
        case 0: \
        default:\
            /* nothing to do */ \
            break; \
        case 1: \
            /* and */ \
            val &= pThis->latch; \
            break; \
        case 2: \
            /* or */ \
            val |= pThis->latch; \
            break; \
        case 3: \
            /* xor */ \
            val ^= pThis->latch; \
            break; \
    } \
    /* apply bit mask */ \
    val = (val & bit_mask) | (pThis->latch & ~bit_mask)

/**
 * Legacy VGA memory (0xa0000 - 0xbffff) write hook, to be called from IOM and from the inside of VGADeviceGC.cpp.
 * This is the advanced version of vga_mem_writeb function.
 *
 * @returns VBox status code.
 * @param   pThis       The shared VGA instance data.
 * @param   pThisCC     The VGA instance data for the current context.
 * @param   pvUser      User argument - ignored.
 * @param   GCPhysAddr  Physical address of memory to write.
 * @param   u32Item     Data to write, up to 4 bytes.
 * @param   cbItem      Size of data Item, only 1/2/4 bytes is allowed for now.
 * @param   cItems      Number of data items to write.
 */
static int vgaInternalMMIOFill(PVGASTATE pThis, PVGASTATECC pThisCC, void *pvUser, RTGCPHYS GCPhysAddr,
                               uint32_t u32Item, unsigned cbItem, unsigned cItems)
{
    uint32_t b;
    uint32_t write_mask, bit_mask, set_mask;
    uint32_t aVal[4];
    unsigned i;
    NOREF(pvUser);

    for (i = 0; i < cbItem; i++)
    {
        aVal[i] = u32Item & 0xff;
        u32Item >>= 8;
    }

    /* convert to VGA memory offset */
    /// @todo add check for the end of region
    GCPhysAddr &= 0x1ffff;
    switch((pThis->gr[6] >> 2) & 3) {
    case 0:
        break;
    case 1:
        if (GCPhysAddr >= 0x10000)
            return VINF_SUCCESS;
        GCPhysAddr += pThis->bank_offset;
        break;
    case 2:
        GCPhysAddr -= 0x10000;
        if (GCPhysAddr >= 0x8000)
            return VINF_SUCCESS;
        break;
    default:
    case 3:
        GCPhysAddr -= 0x18000;
        if (GCPhysAddr >= 0x8000)
            return VINF_SUCCESS;
        break;
    }

    if (pThis->sr[4] & 0x08) {
        /* chain 4 mode : simplest access */
        VERIFY_VRAM_WRITE_OFF_RETURN(pThis, GCPhysAddr + cItems * cbItem - 1);

        while (cItems-- > 0)
            for (i = 0; i < cbItem; i++)
            {
                if (pThis->sr[2] & (1 << (GCPhysAddr & 3)))
                {
                    pThisCC->pbVRam[GCPhysAddr] = aVal[i];
                    vgaR3MarkDirty(pThis, GCPhysAddr);
                }
                GCPhysAddr++;
            }
    } else if (pThis->gr[5] & 0x10) {
        /* odd/even mode (aka text mode mapping) */
        VERIFY_VRAM_WRITE_OFF_RETURN(pThis, (GCPhysAddr + cItems * cbItem) * 4 - 1);
        while (cItems-- > 0)
            for (i = 0; i < cbItem; i++)
            {
                unsigned plane = GCPhysAddr & 1;
                if (pThis->sr[2] & (1 << plane)) {
                    RTGCPHYS PhysAddr2 = ((GCPhysAddr & ~1) * 4) | plane;
                    pThisCC->pbVRam[PhysAddr2] = aVal[i];
                    vgaR3MarkDirty(pThis, PhysAddr2);
                }
                GCPhysAddr++;
            }
    } else {
        /* standard VGA latched access */
        VERIFY_VRAM_WRITE_OFF_RETURN(pThis, (GCPhysAddr + cItems * cbItem) * 4 - 1);

        switch(pThis->gr[5] & 3) {
        default:
        case 0:
            /* rotate */
            b = pThis->gr[3] & 7;
            bit_mask = pThis->gr[8];
            bit_mask |= bit_mask << 8;
            bit_mask |= bit_mask << 16;
            set_mask = mask16[pThis->gr[1]];

            for (i = 0; i < cbItem; i++)
            {
                aVal[i] = ((aVal[i] >> b) | (aVal[i] << (8 - b))) & 0xff;
                aVal[i] |= aVal[i] << 8;
                aVal[i] |= aVal[i] << 16;

                /* apply set/reset mask */
                aVal[i] = (aVal[i] & ~set_mask) | (mask16[pThis->gr[0]] & set_mask);

                APPLY_LOGICAL_AND_MASK(pThis, aVal[i], bit_mask);
            }
            break;
        case 1:
            for (i = 0; i < cbItem; i++)
                aVal[i] = pThis->latch;
            break;
        case 2:
            bit_mask = pThis->gr[8];
            bit_mask |= bit_mask << 8;
            bit_mask |= bit_mask << 16;
            for (i = 0; i < cbItem; i++)
            {
                aVal[i] = mask16[aVal[i] & 0x0f];

                APPLY_LOGICAL_AND_MASK(pThis, aVal[i], bit_mask);
            }
            break;
        case 3:
            /* rotate */
            b = pThis->gr[3] & 7;

            for (i = 0; i < cbItem; i++)
            {
                aVal[i] = (aVal[i] >> b) | (aVal[i] << (8 - b));
                bit_mask = pThis->gr[8] & aVal[i];
                bit_mask |= bit_mask << 8;
                bit_mask |= bit_mask << 16;
                aVal[i] = mask16[pThis->gr[0]];

                APPLY_LOGICAL_AND_MASK(pThis, aVal[i], bit_mask);
            }
            break;
        }

        /* mask data according to sr[2] */
        write_mask = mask16[pThis->sr[2]];

        /* actually write data */
        if (cbItem == 1)
        {
            /* The most frequently case is 1 byte I/O. */
            while (cItems-- > 0)
            {
                ((uint32_t *)pThisCC->pbVRam)[GCPhysAddr] = (((uint32_t *)pThisCC->pbVRam)[GCPhysAddr] & ~write_mask) | (aVal[0] & write_mask);
                vgaR3MarkDirty(pThis, GCPhysAddr * 4);
                GCPhysAddr++;
            }
        }
        else if (cbItem == 2)
        {
            /* The second case is 2 bytes I/O. */
            while (cItems-- > 0)
            {
                ((uint32_t *)pThisCC->pbVRam)[GCPhysAddr] = (((uint32_t *)pThisCC->pbVRam)[GCPhysAddr] & ~write_mask) | (aVal[0] & write_mask);
                vgaR3MarkDirty(pThis, GCPhysAddr * 4);
                GCPhysAddr++;

                ((uint32_t *)pThisCC->pbVRam)[GCPhysAddr] = (((uint32_t *)pThisCC->pbVRam)[GCPhysAddr] & ~write_mask) | (aVal[1] & write_mask);
                vgaR3MarkDirty(pThis, GCPhysAddr * 4);
                GCPhysAddr++;
            }
        }
        else
        {
            /* And the rest is 4 bytes. */
            Assert(cbItem == 4);
            while (cItems-- > 0)
                for (i = 0; i < cbItem; i++)
                {
                    ((uint32_t *)pThisCC->pbVRam)[GCPhysAddr] = (((uint32_t *)pThisCC->pbVRam)[GCPhysAddr] & ~write_mask) | (aVal[i] & write_mask);
                    vgaR3MarkDirty(pThis, GCPhysAddr * 4);
                    GCPhysAddr++;
                }
        }
    }
    return VINF_SUCCESS;
}

#undef APPLY_LOGICAL_AND_MASK

/**
 * @callback_method_impl{FNIOMMMIONEWFILL,
 * Legacy VGA memory (0xa0000 - 0xbffff) write hook\, to be called from IOM and
 * from the inside of VGADeviceGC.cpp. This is the advanced version of
 * vga_mem_writeb function.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vgaMmioFill(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, uint32_t u32Item, unsigned cbItem, unsigned cItems)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

    return vgaInternalMMIOFill(pThis, pThisCC, pvUser, off, u32Item, cbItem, cItems);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD,
 * Legacy VGA memory (0xa0000 - 0xbffff) read hook\, to be called from IOM.}
 *
 * @note The @a off is an absolute address in the 0xa0000 - 0xbffff range, not
 *       an offset.
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    STAM_PROFILE_START(&pThis->CTX_MID_Z(Stat,MemoryRead), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    NOREF(pvUser);

    int rc = VINF_SUCCESS;
    switch (cb)
    {
        case 1:
            *(uint8_t  *)pv = vga_mem_readb(pDevIns, pThis, pThisCC, off, &rc);
            break;
        case 2:
/** @todo This and the wider accesses maybe misbehave when accessing bytes
 *        crossing the 512KB VRAM boundrary if the access is handled in
 *        ring-0 and operating in latched mode. */
            *(uint16_t *)pv = vga_mem_readb(pDevIns, pThis, pThisCC, off, &rc)
                           | (vga_mem_readb(pDevIns, pThis, pThisCC, off + 1, &rc) << 8);
            break;
        case 4:
            *(uint32_t *)pv = vga_mem_readb(pDevIns, pThis, pThisCC, off, &rc)
                           | (vga_mem_readb(pDevIns, pThis, pThisCC, off + 1, &rc) <<  8)
                           | (vga_mem_readb(pDevIns, pThis, pThisCC, off + 2, &rc) << 16)
                           | (vga_mem_readb(pDevIns, pThis, pThisCC, off + 3, &rc) << 24);
            break;

        case 8:
            *(uint64_t *)pv = (uint64_t)vga_mem_readb(pDevIns, pThis, pThisCC, off, &rc)
                           | ((uint64_t)vga_mem_readb(pDevIns, pThis, pThisCC, off + 1, &rc) <<  8)
                           | ((uint64_t)vga_mem_readb(pDevIns, pThis, pThisCC, off + 2, &rc) << 16)
                           | ((uint64_t)vga_mem_readb(pDevIns, pThis, pThisCC, off + 3, &rc) << 24)
                           | ((uint64_t)vga_mem_readb(pDevIns, pThis, pThisCC, off + 4, &rc) << 32)
                           | ((uint64_t)vga_mem_readb(pDevIns, pThis, pThisCC, off + 5, &rc) << 40)
                           | ((uint64_t)vga_mem_readb(pDevIns, pThis, pThisCC, off + 6, &rc) << 48)
                           | ((uint64_t)vga_mem_readb(pDevIns, pThis, pThisCC, off + 7, &rc) << 56);
            break;

        default:
        {
            uint8_t *pbData = (uint8_t *)pv;
            while (cb-- > 0)
            {
                *pbData++ = vga_mem_readb(pDevIns, pThis, pThisCC, off++, &rc);
                if (RT_UNLIKELY(rc != VINF_SUCCESS))
                    break;
            }
        }
    }

    STAM_PROFILE_STOP(&pThis->CTX_MID_Z(Stat,MemoryRead), a);
    return rc;
}

/**
 * @callback_method_impl{FNIOMMMIONEWWRITE,
 * Legacy VGA memory (0xa0000 - 0xbffff) write hook\, to be called from IOM.}
 *
 * @note The @a off is an absolute address in the 0xa0000 - 0xbffff range, not
 *       an offset.
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PVGASTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    uint8_t const *pbSrc  = (uint8_t const *)pv;
    NOREF(pvUser);
    STAM_PROFILE_START(&pThis->CTX_MID_Z(Stat,MemoryWrite), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

    VBOXSTRICTRC rc;
    switch (cb)
    {
        case 1:
            rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off, *pbSrc);
            break;
#if 1
        case 2:
            rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 0, pbSrc[0]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 1, pbSrc[1]);
            break;
        case 4:
            rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 0, pbSrc[0]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 1, pbSrc[1]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 2, pbSrc[2]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 3, pbSrc[3]);
            break;
        case 8:
            rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 0, pbSrc[0]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 1, pbSrc[1]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 2, pbSrc[2]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 3, pbSrc[3]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 4, pbSrc[4]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 5, pbSrc[5]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 6, pbSrc[6]);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off + 7, pbSrc[7]);
            break;
#else
        case 2:
            rc = vgaMmioFill(pDevIns, off, *(uint16_t *)pv, 2, 1);
            break;
        case 4:
            rc = vgaMmioFill(pDevIns, off, *(uint32_t *)pv, 4, 1);
            break;
        case 8:
            rc = vgaMmioFill(pDevIns, off, *(uint64_t *)pv, 8, 1);
            break;
#endif
        default:
            rc = VINF_SUCCESS;
            while (cb-- > 0 && rc == VINF_SUCCESS)
                rc = vga_mem_writeb(pDevIns, pThis, pThisCC, off++, *pbSrc++);
            break;

    }
    STAM_PROFILE_STOP(&pThis->CTX_MID_Z(Stat,MemoryWrite), a);
    return rc;
}


/* -=-=-=-=-=- All rings: VGA BIOS I/Os -=-=-=-=-=- */

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 *      Port I/O Handler for VGA BIOS IN operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortReadBios(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pDevIns, pvUser, offPort, pu32, cb);
    return VERR_IOM_IOPORT_UNUSED;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 *      Port I/O Handler for VGA BIOS IN operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vgaIoPortWriteBios(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF2(pDevIns, pvUser);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    Assert(offPort == 0); RT_NOREF(offPort);

    /*
     * VGA BIOS char printing.
     */
    if (cb == 1)
    {
#if 0
        switch (u32)
        {
            case '\r': Log(("vgabios: <return>\n")); break;
            case '\n': Log(("vgabios: <newline>\n")); break;
            case '\t': Log(("vgabios: <tab>\n")); break;
            default:
                Log(("vgabios: %c\n", u32));
        }
#else
        static int s_fLastWasNotNewline = 0;  /* We are only called in a single-threaded way */
        if (s_fLastWasNotNewline == 0)
            Log(("vgabios: "));
        if (u32 != '\r')  /* return - is only sent in conjunction with '\n' */
            Log(("%c", u32));
        if (u32 == '\n')
            s_fLastWasNotNewline = 0;
        else
            s_fLastWasNotNewline = 1;
#endif
        return VINF_SUCCESS;
    }

    /* not in use. */
    return VERR_IOM_IOPORT_UNUSED;
}


/* -=-=-=-=-=- Ring 3 -=-=-=-=-=- */

#ifdef IN_RING3

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 *      Port I/O Handler for VBE Extra OUT operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vbeR3IOPortWriteVbeExtra(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    RT_NOREF(offPort, pvUser);

    if (cb == 2)
    {
        Log(("vbeR3IOPortWriteVbeExtra: addr=%#RX32\n", u32));
        pThisCC->u16VBEExtraAddress = u32;
    }
    else
        Log(("vbeR3IOPortWriteVbeExtra: Ignoring invalid cb=%d writes to the VBE Extra port!!!\n", cb));

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 *      Port I/O Handler for VBE Extra IN operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vbeR3IoPortReadVbeExtra(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    RT_NOREF(offPort, pvUser);

    int rc = VINF_SUCCESS;
    if (pThisCC->u16VBEExtraAddress == 0xffff)
    {
        Log(("vbeR3IoPortReadVbeExtra: Requested number of 64k video banks\n"));
        *pu32 = pThis->vram_size / _64K;
    }
    else if (   pThisCC->u16VBEExtraAddress >= pThisCC->cbVBEExtraData
             || pThisCC->u16VBEExtraAddress + cb > pThisCC->cbVBEExtraData)
    {
        *pu32 = 0;
        Log(("vbeR3IoPortReadVbeExtra: Requested address is out of VBE data!!! Address=%#x(%d) cbVBEExtraData=%#x(%d)\n",
             pThisCC->u16VBEExtraAddress, pThisCC->u16VBEExtraAddress, pThisCC->cbVBEExtraData, pThisCC->cbVBEExtraData));
    }
    else
    {
        RT_UNTRUSTED_VALIDATED_FENCE();
        if (cb == 1)
        {
            *pu32 = pThisCC->pbVBEExtraData[pThisCC->u16VBEExtraAddress] & 0xFF;

            Log(("vbeR3IoPortReadVbeExtra: cb=%#x %.*Rhxs\n", cb, cb, pu32));
        }
        else if (cb == 2)
        {
            *pu32 =           pThisCC->pbVBEExtraData[pThisCC->u16VBEExtraAddress]
                  | (uint32_t)pThisCC->pbVBEExtraData[pThisCC->u16VBEExtraAddress + 1] << 8;

            Log(("vbeR3IoPortReadVbeExtra: cb=%#x %.*Rhxs\n", cb, cb, pu32));
        }
        else
        {
            Log(("vbeR3IoPortReadVbeExtra: Invalid cb=%d read from the VBE Extra port!!!\n", cb));
            rc = VERR_IOM_IOPORT_UNUSED;
        }
    }

    return rc;
}


/**
 * Parse the logo bitmap data at init time.
 *
 * @returns VBox status code.
 *
 * @param   pThisCC     The VGA instance data for ring-3.
 */
static int vbeR3ParseBitmap(PVGASTATECC pThisCC)
{
    /*
     * Get bitmap header data
     */
    PCLOGOHDR        pLogoHdr = (PCLOGOHDR)pThisCC->pbLogo;
    PBMPFILEHDR      pFileHdr = (PBMPFILEHDR)(pThisCC->pbLogo + sizeof(LOGOHDR));
    PBMPWIN3XINFOHDR pCoreHdr = (PBMPWIN3XINFOHDR)(pThisCC->pbLogo + sizeof(LOGOHDR) + sizeof(BMPFILEHDR));

    if (pFileHdr->uType == BMP_HDR_MAGIC)
    {
        switch (pCoreHdr->cbSize)
        {
            case BMP_HDR_SIZE_OS21:
            {
                PBMPOS2COREHDR pOs2Hdr = (PBMPOS2COREHDR)pCoreHdr;
                pThisCC->cxLogo = pOs2Hdr->uWidth;
                pThisCC->cyLogo = pOs2Hdr->uHeight;
                pThisCC->cLogoPlanes = pOs2Hdr->cPlanes;
                pThisCC->cLogoBits = pOs2Hdr->cBits;
                pThisCC->LogoCompression = BMP_COMPRESSION_TYPE_NONE;
                pThisCC->cLogoUsedColors = 0;
                break;
            }

            case BMP_HDR_SIZE_OS22:
            {
                PBMPOS2COREHDR2 pOs22Hdr = (PBMPOS2COREHDR2)pCoreHdr;
                pThisCC->cxLogo = pOs22Hdr->uWidth;
                pThisCC->cyLogo = pOs22Hdr->uHeight;
                pThisCC->cLogoPlanes = pOs22Hdr->cPlanes;
                pThisCC->cLogoBits = pOs22Hdr->cBits;
                pThisCC->LogoCompression = pOs22Hdr->enmCompression;
                pThisCC->cLogoUsedColors = pOs22Hdr->cClrUsed;
                break;
            }

            case BMP_HDR_SIZE_WIN3X:
                pThisCC->cxLogo = pCoreHdr->uWidth;
                pThisCC->cyLogo = pCoreHdr->uHeight;
                pThisCC->cLogoPlanes = pCoreHdr->cPlanes;
                pThisCC->cLogoBits = pCoreHdr->cBits;
                pThisCC->LogoCompression = pCoreHdr->enmCompression;
                pThisCC->cLogoUsedColors = pCoreHdr->cClrUsed;
                break;

            default:
                AssertLogRelMsgFailedReturn(("Unsupported bitmap header size %u.\n", pCoreHdr->cbSize),
                                            VERR_INVALID_PARAMETER);
                break;
        }

        AssertLogRelMsgReturn(pThisCC->cxLogo <= LOGO_MAX_WIDTH && pThisCC->cyLogo <= LOGO_MAX_HEIGHT,
                              ("Bitmap %ux%u is too big.\n", pThisCC->cxLogo, pThisCC->cyLogo),
                              VERR_INVALID_PARAMETER);

        AssertLogRelMsgReturn(pThisCC->cLogoPlanes == 1,
                              ("Bitmap planes %u != 1.\n", pThisCC->cLogoPlanes),
                              VERR_INVALID_PARAMETER);

        AssertLogRelMsgReturn(pThisCC->cLogoBits == 4 || pThisCC->cLogoBits == 8 || pThisCC->cLogoBits == 24,
                              ("Unsupported %u depth.\n", pThisCC->cLogoBits),
                              VERR_INVALID_PARAMETER);

        AssertLogRelMsgReturn(pThisCC->cLogoUsedColors <= 256,
                              ("Unsupported %u colors.\n", pThisCC->cLogoUsedColors),
                              VERR_INVALID_PARAMETER);

        AssertLogRelMsgReturn(pThisCC->LogoCompression == BMP_COMPRESSION_TYPE_NONE,
                              ("Unsupported %u compression.\n", pThisCC->LogoCompression),
                              VERR_INVALID_PARAMETER);

        AssertLogRelMsgReturn(pLogoHdr->cbLogo > pFileHdr->offBits,
                              ("Wrong bitmap data offset %u, cbLogo=%u.\n", pFileHdr->offBits, pLogoHdr->cbLogo),
                              VERR_INVALID_PARAMETER);

        uint32_t const cbFileData  = pLogoHdr->cbLogo - pFileHdr->offBits;
        uint32_t       cbImageData = (uint32_t)pThisCC->cxLogo * pThisCC->cyLogo * pThisCC->cLogoPlanes;
        if (pThisCC->cLogoBits == 4)
            cbImageData /= 2;
        else if (pThisCC->cLogoBits == 24)
            cbImageData *= 3;
        AssertLogRelMsgReturn(cbImageData <= cbFileData,
                              ("Wrong BMP header data %u (cbLogo=%u offBits=%u)\n", cbImageData, pFileHdr->offBits, pLogoHdr->cbLogo),
                              VERR_INVALID_PARAMETER);

        AssertLogRelMsgReturn(pLogoHdr->cbLogo == pFileHdr->cbFileSize,
                              ("Wrong bitmap file size %u, cbLogo=%u.\n", pFileHdr->cbFileSize, pLogoHdr->cbLogo),
                              VERR_INVALID_PARAMETER);

        /*
         * Read bitmap palette
         */
        if (!pThisCC->cLogoUsedColors)
            pThisCC->cLogoPalEntries = 1 << (pThisCC->cLogoPlanes * pThisCC->cLogoBits);
        else
            pThisCC->cLogoPalEntries = pThisCC->cLogoUsedColors;

        if (pThisCC->cLogoPalEntries)
        {
            const uint8_t *pbPal = pThisCC->pbLogo + sizeof(LOGOHDR) + sizeof(BMPFILEHDR) + pCoreHdr->cbSize; /* ASSUMES Size location (safe) */

            for (uint16_t i = 0; i < pThisCC->cLogoPalEntries; i++)
            {
                uint16_t j;
                uint32_t u32Pal = 0;

                for (j = 0; j < 3; j++)
                {
                    uint8_t b = *pbPal++;
                    u32Pal <<= 8;
                    u32Pal |= b;
                }

                pbPal++; /* skip unused byte */
                pThisCC->au32LogoPalette[i] = u32Pal;
            }
        }

        /*
         * Bitmap data offset
         */
        pThisCC->pbLogoBitmap = pThisCC->pbLogo + sizeof(LOGOHDR) + pFileHdr->offBits;
    }
    else
        AssertLogRelMsgFailedReturn(("Not a BMP file.\n"), VERR_INVALID_PARAMETER);

    return VINF_SUCCESS;
}


/**
 * Show logo bitmap data.
 *
 * @param   cBits       Logo depth.
 * @param   xLogo       Logo X position.
 * @param   yLogo       Logo Y position.
 * @param   cxLogo      Logo width.
 * @param   cyLogo      Logo height.
 * @param   fInverse    True if the bitmask is black on white (only for 1bpp)
 * @param   iStep       Fade in/fade out step.
 * @param   pu32Palette Palette data.
 * @param   pbSrc       Source buffer.
 * @param   pbDst       Destination buffer.
 */
static void vbeR3ShowBitmap(uint16_t cBits, uint16_t xLogo, uint16_t yLogo, uint16_t cxLogo, uint16_t cyLogo,
                            bool fInverse, uint8_t iStep, const uint32_t *pu32Palette, const uint8_t *pbSrc, uint8_t *pbDst)
{
    uint16_t        i;
    size_t          cbPadBytes  = 0;
    size_t          cbLineDst   = LOGO_MAX_WIDTH * 4;
    uint16_t        cyLeft      = cyLogo;

    pbDst += xLogo * 4 + yLogo * cbLineDst;

    switch (cBits)
    {
        case 1:
            pbDst += cyLogo * cbLineDst;
            cbPadBytes = 0;
            break;

        case 4:
            if (((cxLogo % 8) == 0) || ((cxLogo % 8) > 6))
                cbPadBytes = 0;
            else if ((cxLogo % 8) <= 2)
                cbPadBytes = 3;
            else if ((cxLogo % 8) <= 4)
                cbPadBytes = 2;
            else
                cbPadBytes = 1;
            break;

        case 8:
            cbPadBytes = ((cxLogo % 4) == 0) ? 0 : (4 - (cxLogo % 4));
            break;

        case 24:
            cbPadBytes = cxLogo % 4;
            break;
    }

    uint8_t j = 0, c = 0;

    while (cyLeft-- > 0)
    {
        uint8_t *pbTmpDst = pbDst;

        if (cBits != 1)
            j = 0;

        for (i = 0; i < cxLogo; i++)
        {
            switch (cBits)
            {
                case 1:
                {
                    if (!j)
                        c = *pbSrc++;

                    if (c & 1)
                    {
                        if (fInverse)
                        {
                            *pbTmpDst++ = 0;
                            *pbTmpDst++ = 0;
                            *pbTmpDst++ = 0;
                            pbTmpDst++;
                        }
                        else
                        {
                            uint8_t pix = 0xFF * iStep / LOGO_SHOW_STEPS;
                            *pbTmpDst++ = pix;
                            *pbTmpDst++ = pix;
                            *pbTmpDst++ = pix;
                            pbTmpDst++;
                        }
                    }
                    else
                        pbTmpDst += 4;
                    c >>= 1;
                    j = (j + 1) % 8;
                    break;
                }

                case 4:
                {
                    if (!j)
                        c = *pbSrc++;

                    uint8_t pix = (c >> 4) & 0xF;
                    c <<= 4;

                    uint32_t u32Pal = pu32Palette[pix];

                    pix = (u32Pal >> 16) & 0xFF;
                    *pbTmpDst++ = pix * iStep / LOGO_SHOW_STEPS;
                    pix = (u32Pal >> 8) & 0xFF;
                    *pbTmpDst++ = pix * iStep / LOGO_SHOW_STEPS;
                    pix = u32Pal & 0xFF;
                    *pbTmpDst++ = pix * iStep / LOGO_SHOW_STEPS;
                    pbTmpDst++;

                    j = (j + 1) % 2;
                    break;
                }

                case 8:
                {
                    uint32_t u32Pal = pu32Palette[*pbSrc++];

                    uint8_t pix = (u32Pal >> 16) & 0xFF;
                    *pbTmpDst++ = pix * iStep / LOGO_SHOW_STEPS;
                    pix = (u32Pal >> 8) & 0xFF;
                    *pbTmpDst++ = pix * iStep / LOGO_SHOW_STEPS;
                    pix = u32Pal & 0xFF;
                    *pbTmpDst++ = pix * iStep / LOGO_SHOW_STEPS;
                    pbTmpDst++;
                    break;
                }

                case 24:
                    *pbTmpDst++ = *pbSrc++ * iStep / LOGO_SHOW_STEPS;
                    *pbTmpDst++ = *pbSrc++ * iStep / LOGO_SHOW_STEPS;
                    *pbTmpDst++ = *pbSrc++ * iStep / LOGO_SHOW_STEPS;
                    pbTmpDst++;
                    break;
            }
        }

        pbDst -= cbLineDst;
        pbSrc += cbPadBytes;
    }
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 *      Port I/O Handler for BIOS Logo OUT operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vbeR3IoPortWriteCmdLogo(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    RT_NOREF(pvUser, offPort);

    Log(("vbeR3IoPortWriteCmdLogo: cb=%d u32=%#04x(%#04d) (byte)\n", cb, u32, u32));

    if (cb == 2)
    {
        /* Get the logo command */
        switch (u32 & 0xFF00)
        {
            case LOGO_CMD_SET_OFFSET:
                pThisCC->offLogoData = u32 & 0xFF;
                break;

            case LOGO_CMD_SHOW_BMP:
            {
                uint8_t         iStep = u32 & 0xFF;
                const uint8_t  *pbSrc = pThisCC->pbLogoBitmap;
                uint8_t        *pbDst;
                PCLOGOHDR       pLogoHdr = (PCLOGOHDR)pThisCC->pbLogo;
                uint32_t        offDirty = 0;
                uint16_t        xLogo = (LOGO_MAX_WIDTH - pThisCC->cxLogo) / 2;
                uint16_t        yLogo = LOGO_MAX_HEIGHT - (LOGO_MAX_HEIGHT - pThisCC->cyLogo) / 2;

                /* Check VRAM size */
                if (pThis->vram_size < LOGO_MAX_SIZE)
                    break;

                if (pThis->vram_size >= LOGO_MAX_SIZE * 2)
                    pbDst = pThisCC->pbVRam + LOGO_MAX_SIZE;
                else
                    pbDst = pThisCC->pbVRam;

                /* Clear screen - except on power on... */
                if (!pThisCC->fLogoClearScreen)
                {
                    /* Clear vram */
                    uint32_t *pu32Dst = (uint32_t *)pbDst;
                    for (int i = 0; i < LOGO_MAX_WIDTH; i++)
                        for (int j = 0; j < LOGO_MAX_HEIGHT; j++)
                            *pu32Dst++ = 0;
                    pThisCC->fLogoClearScreen = true;
                }

                /* Show the bitmap. */
                vbeR3ShowBitmap(pThisCC->cLogoBits, xLogo, yLogo,
                                pThisCC->cxLogo, pThisCC->cyLogo,
                                false, iStep, &pThisCC->au32LogoPalette[0],
                                pbSrc, pbDst);

                /* Show the 'Press F12...' text. */
                if (pLogoHdr->fu8ShowBootMenu == 2)
                    vbeR3ShowBitmap(1, LOGO_F12TEXT_X, LOGO_F12TEXT_Y,
                                    LOGO_F12TEXT_WIDTH, LOGO_F12TEXT_HEIGHT,
                                    pThisCC->fBootMenuInverse, iStep, &pThisCC->au32LogoPalette[0],
                                    &g_abLogoF12BootText[0], pbDst);

                /* Blit the offscreen buffer. */
                if (pThis->vram_size >= LOGO_MAX_SIZE * 2)
                {
                    uint32_t *pu32TmpDst = (uint32_t *)pThisCC->pbVRam;
                    uint32_t *pu32TmpSrc = (uint32_t *)(pThisCC->pbVRam + LOGO_MAX_SIZE);
                    for (int i = 0; i < LOGO_MAX_WIDTH; i++)
                    {
                        for (int j = 0; j < LOGO_MAX_HEIGHT; j++)
                            *pu32TmpDst++ = *pu32TmpSrc++;
                    }
                }

                /* Set the dirty flags. */
                while (offDirty <= LOGO_MAX_SIZE)
                {
                    vgaR3MarkDirty(pThis, offDirty);
                    offDirty += GUEST_PAGE_SIZE;
                }
                break;
            }

            default:
                Log(("vbeR3IoPortWriteCmdLogo: invalid command %d\n", u32));
                pThisCC->LogoCommand = LOGO_CMD_NOP;
                break;
        }

        return VINF_SUCCESS;
    }

    Log(("vbeR3IoPortWriteCmdLogo: Ignoring invalid cb=%d writes to the VBE Extra port!!!\n", cb));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTIN,
 *      Port I/O Handler for BIOS Logo IN operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vbeR3IoPortReadCmdLogo(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    RT_NOREF(pvUser, offPort);

    if (pThisCC->offLogoData + cb > pThisCC->cbLogo)
    {
        Log(("vbeR3IoPortReadCmdLogo: Requested address is out of Logo data!!! offLogoData=%#x(%d) cbLogo=%#x(%d)\n",
             pThisCC->offLogoData, pThisCC->offLogoData, pThisCC->cbLogo, pThisCC->cbLogo));
        return VINF_SUCCESS;
    }
    RT_UNTRUSTED_VALIDATED_FENCE();

    PCRTUINT64U p = (PCRTUINT64U)&pThisCC->pbLogo[pThisCC->offLogoData];
    switch (cb)
    {
        case 1: *pu32 = p->au8[0]; break;
        case 2: *pu32 = p->au16[0]; break;
        case 4: *pu32 = p->au32[0]; break;
        //case 8: *pu32 = p->au64[0]; break;
        default: AssertFailed(); break;
    }
    Log(("vbeR3IoPortReadCmdLogo: LogoOffset=%#x(%d) cb=%#x %.*Rhxs\n", pThisCC->offLogoData, pThisCC->offLogoData, cb, cb, pu32));

    pThisCC->LogoCommand = LOGO_CMD_NOP;
    pThisCC->offLogoData += cb;

    return VINF_SUCCESS;
}


/* -=-=-=-=-=- Ring 3: Debug Info Handlers -=-=-=-=-=- */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV,
 *      Dumps several interesting bits of the VGA state that are difficult to
 *      decode from the registers.}
 */
static DECLCALLBACK(void) vgaR3InfoState(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    int             is_graph, double_scan;
    int             w, h, char_height, char_dots;
    int             val, vfreq_hz, hfreq_hz;
    vga_retrace_s   *r = &pThis->retrace_state;
    const char      *clocks[] = { "25.175 MHz", "28.322 MHz", "External", "Reserved?!" };
    const char      *mem_map[] = { "A000-BFFF", "A000-AFFF", "B000-B7FF", "B800-BFFF" };
    NOREF(pszArgs);

    is_graph  = pThis->gr[6] & 1;
    char_dots = is_graph ? 8 : (pThis->sr[0x01] & 1) ? 8 : 9;
    double_scan = pThis->cr[9] >> 7;
    pHlp->pfnPrintf(pHlp, "decoding memory at %s\n", mem_map[(pThis->gr[6] >> 2) & 3]);
    pHlp->pfnPrintf(pHlp, "Misc status reg. MSR:%02X\n", pThis->msr);
    pHlp->pfnPrintf(pHlp, "pixel clock: %s\n", clocks[(pThis->msr >> 2) & 3]);
    pHlp->pfnPrintf(pHlp, "double scanning %s\n", double_scan ? "on" : "off");
    pHlp->pfnPrintf(pHlp, "double clocking %s\n", pThis->sr[1] & 0x08 ? "on" : "off");
    val = pThis->cr[0] + 5;
    pHlp->pfnPrintf(pHlp, "htotal: %d px (%d cclk)\n", val * char_dots, val);
    val = pThis->cr[6] + ((pThis->cr[7] & 1) << 8) + ((pThis->cr[7] & 0x20) << 4) + 2;
    pHlp->pfnPrintf(pHlp, "vtotal: %d px\n", val);
    val = pThis->cr[1] + 1;
    w   = val * char_dots;
    pHlp->pfnPrintf(pHlp, "hdisp : %d px (%d cclk)\n", w, val);
    val = pThis->cr[0x12] + ((pThis->cr[7] & 2) << 7) + ((pThis->cr[7] & 0x40) << 3) + 1;
    h   = val;
    pHlp->pfnPrintf(pHlp, "vdisp : %d px\n", val);
    val = ((pThis->cr[9] & 0x40) << 3) + ((pThis->cr[7] & 0x10) << 4) + pThis->cr[0x18];
    pHlp->pfnPrintf(pHlp, "split : %d ln\n", val);
    val = (pThis->cr[0xc] << 8) + pThis->cr[0xd];
    pHlp->pfnPrintf(pHlp, "start : %#x\n", val);
    if (!is_graph)
    {
        uint8_t ch_stride;

        ch_stride = pThis->cr[0x17] & 0x40 ? 4 : 8;
        val = (pThis->cr[9] & 0x1f) + 1;
        char_height = val;
        pHlp->pfnPrintf(pHlp, "char height %d\n", val);
        pHlp->pfnPrintf(pHlp, "text mode %dx%d\n", w / char_dots, h / (char_height << double_scan));

        uint32_t cbLine;
        uint32_t offStart;
        uint32_t offCursr;
        uint32_t uLineCompareIgn;
        vgaR3GetOffsets(pThis, &cbLine, &offStart, &uLineCompareIgn);
        if (!cbLine)
            cbLine = 80 * ch_stride;
        offStart *= ch_stride;
        offCursr = ((pThis->cr[0x0e] << 8) | pThis->cr[0x0f]) * ch_stride;
        pHlp->pfnPrintf(pHlp, "cbLine:   %#x\n", cbLine);
        pHlp->pfnPrintf(pHlp, "offStart: %#x (line %#x)\n", offStart, offStart / cbLine);
        pHlp->pfnPrintf(pHlp, "offCursr: %#x\n", offCursr);
    }
    if (pThis->fRealRetrace)
    {
        val = r->hb_start;
        pHlp->pfnPrintf(pHlp, "hblank start: %d px (%d cclk)\n", val * char_dots, val);
        val = r->hb_end;
        pHlp->pfnPrintf(pHlp, "hblank end  : %d px (%d cclk)\n", val * char_dots, val);
        pHlp->pfnPrintf(pHlp, "vblank start: %d px, end: %d px\n", r->vb_start, r->vb_end);
        pHlp->pfnPrintf(pHlp, "vsync start : %d px, end: %d px\n", r->vs_start, r->vs_end);
        pHlp->pfnPrintf(pHlp, "cclks per frame: %d\n", r->frame_cclks);
        pHlp->pfnPrintf(pHlp, "cclk time (ns) : %d\n", r->cclk_ns);
        if (r->frame_ns && r->h_total_ns)   /* Careful in case state is temporarily invalid. */
        {
            vfreq_hz = 1000000000 / r->frame_ns;
            hfreq_hz = 1000000000 / r->h_total_ns;
            pHlp->pfnPrintf(pHlp, "vfreq: %d Hz, hfreq: %d.%03d kHz\n",
                            vfreq_hz, hfreq_hz / 1000, hfreq_hz % 1000);
        }
    }
    pHlp->pfnPrintf(pHlp, "display refresh interval: %u ms\n", pThis->cMilliesRefreshInterval);

# ifdef VBOX_WITH_VMSVGA
    if (pThis->svga.fEnabled)
        pHlp->pfnPrintf(pHlp, pThis->svga.f3DEnabled ? "VMSVGA 3D enabled: %ux%ux%u\n" : "VMSVGA enabled: %ux%ux%u",
                        pThis->svga.uWidth, pThis->svga.uHeight, pThis->svga.uBpp);
# endif
}


/**
 * Prints a separator line.
 *
 * @param   pHlp                Callback functions for doing output.
 * @param   cCols               The number of columns.
 * @param   pszTitle            The title text, NULL if none.
 */
static void vgaR3InfoTextPrintSeparatorLine(PCDBGFINFOHLP pHlp, size_t cCols, const char *pszTitle)
{
    if (pszTitle)
    {
        size_t cchTitle = strlen(pszTitle);
        if (cchTitle + 6 >= cCols)
        {
            pHlp->pfnPrintf(pHlp, "-- %s --", pszTitle);
            cCols = 0;
        }
        else
        {
            size_t cchLeft = (cCols - cchTitle - 2) / 2;
            cCols -= cchLeft + cchTitle + 2;
            while (cchLeft-- > 0)
                pHlp->pfnPrintf(pHlp, "-");
            pHlp->pfnPrintf(pHlp, " %s ", pszTitle);
        }
    }

    while (cCols-- > 0)
        pHlp->pfnPrintf(pHlp, "-");
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * Worker for vgaR3InfoText.
 *
 * @param   pThis       The shared VGA state.
 * @param   pThisCC     The VGA state for ring-3.
 * @param   pHlp        Callback functions for doing output.
 * @param   offStart    Where to start dumping (relative to the VRAM).
 * @param   cbLine      The source line length (aka line_offset).
 * @param   cCols       The number of columns on the screen.
 * @param   cRows       The number of rows to dump.
 * @param   iScrBegin   The row at which the current screen output starts.
 * @param   iScrEnd     The row at which the current screen output end
 *                      (exclusive).
 */
static void vgaR3InfoTextWorker(PVGASTATE pThis, PVGASTATER3 pThisCC, PCDBGFINFOHLP pHlp,
                                uint32_t offStart, uint32_t cbLine,
                                uint32_t cCols, uint32_t cRows,
                                uint32_t iScrBegin, uint32_t iScrEnd)
{
    /* Title, */
    char szTitle[32];
    if (iScrBegin || iScrEnd < cRows)
        RTStrPrintf(szTitle, sizeof(szTitle), "%ux%u (+%u before, +%u after)",
                    cCols, iScrEnd - iScrBegin, iScrBegin, cRows - iScrEnd);
    else
        RTStrPrintf(szTitle, sizeof(szTitle), "%ux%u", cCols, iScrEnd - iScrBegin);

    /* Do the dumping. */
    uint8_t const *pbSrcOuter = pThisCC->pbVRam + offStart;
    uint8_t const cStride = pThis->cr[0x17] & 0x40 ? 4 : 8;
    uint32_t iRow;
    for (iRow = 0; iRow < cRows; iRow++, pbSrcOuter += cbLine)
    {
        if ((uintptr_t)(pbSrcOuter + cbLine - pThisCC->pbVRam) > pThis->vram_size) {
            pHlp->pfnPrintf(pHlp, "The last %u row/rows is/are outside the VRAM.\n", cRows - iRow);
            break;
        }

        if (iRow == 0)
            vgaR3InfoTextPrintSeparatorLine(pHlp, cCols, szTitle);
        else if (iRow == iScrBegin)
            vgaR3InfoTextPrintSeparatorLine(pHlp, cCols, "screen start");
        else if (iRow == iScrEnd)
            vgaR3InfoTextPrintSeparatorLine(pHlp, cCols, "screen end");

        uint8_t const *pbSrc = pbSrcOuter;
        for (uint32_t iCol = 0; iCol < cCols; ++iCol)
        {
            if (RT_C_IS_PRINT(*pbSrc))
                pHlp->pfnPrintf(pHlp, "%c", *pbSrc);
            else
                pHlp->pfnPrintf(pHlp, ".");
            pbSrc += cStride;   /* chars are spaced 8 or sometimes 4 bytes apart */
        }
        pHlp->pfnPrintf(pHlp, "\n");
    }

    /* Final separator. */
    vgaR3InfoTextPrintSeparatorLine(pHlp, cCols, NULL);
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV,
 *      Dumps VGA memory formatted as ASCII text\, no attributes. Only looks at
 *      the first page.}
 */
static DECLCALLBACK(void) vgaR3InfoText(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);

    /*
     * Parse args.
     */
    bool fAll = true;
    if (pszArgs && *pszArgs)
    {
        if (!strcmp(pszArgs, "all"))
            fAll = true;
        else if (!strcmp(pszArgs, "scr") || !strcmp(pszArgs, "screen"))
            fAll = false;
        else
        {
            pHlp->pfnPrintf(pHlp, "Invalid argument: '%s'\n", pszArgs);
            return;
        }
    }

    /*
     * Check that we're in text mode and that the VRAM is accessible.
     */
    if (!(pThis->gr[6] & 1))
    {
        uint8_t *pbSrc = pThisCC->pbVRam;
        if (pbSrc)
        {
            /*
             * Figure out the display size and where the text is.
             *
             * Note! We're cutting quite a few corners here and this code could
             *       do with some brushing up.  Dumping from the start of the
             *       frame buffer is done intentionally so that we're more
             *       likely to obtain the full scrollback of a linux panic.
             * windbg> .printf "------ start -----\n"; .for (r $t0 = 0; @$t0 < 25; r $t0 = @$t0 + 1) { .for (r $t1 = 0; @$t1 < 80; r $t1 = @$t1 + 1) { .printf "%c", by( (@$t0 * 80 + @$t1) * 8 + 100f0000) }; .printf "\n" }; .printf "------ end -----\n";
             */
            uint32_t cbLine;
            uint32_t offStart;
            uint32_t uLineCompareIgn;
            vgaR3GetOffsets(pThis, &cbLine, &offStart, &uLineCompareIgn);
            if (!cbLine)
                cbLine = 80 * 8;
            offStart *= 8;

            uint32_t uVDisp      = pThis->cr[0x12] + ((pThis->cr[7] & 2) << 7) + ((pThis->cr[7] & 0x40) << 4) + 1;
            uint32_t uCharHeight = (pThis->cr[9] & 0x1f) + 1;
            uint32_t uDblScan    = pThis->cr[9] >> 7;
            uint32_t cScrRows    = uVDisp / (uCharHeight << uDblScan);
            if (cScrRows < 25)
                cScrRows = 25;
            uint32_t iScrBegin   = offStart / cbLine;
            uint32_t cRows       = iScrBegin + cScrRows;
            uint32_t cCols       = cbLine / 8;

            if (fAll)
                vgaR3InfoTextWorker(pThis, pThisCC, pHlp, offStart - iScrBegin * cbLine, cbLine,
                                    cCols, cRows, iScrBegin, iScrBegin + cScrRows);
            else
                vgaR3InfoTextWorker(pThis, pThisCC, pHlp, offStart, cbLine, cCols, cScrRows, 0, cScrRows);
        }
        else
            pHlp->pfnPrintf(pHlp, "VGA memory not available!\n");
    }
    else
        pHlp->pfnPrintf(pHlp, "Not in text mode!\n");
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, Dumps VGA Sequencer registers.}
 */
static DECLCALLBACK(void) vgaR3InfoSR(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "VGA Sequencer (3C5): SR index 3C4:%02X\n", pThis->sr_index);
    Assert(sizeof(pThis->sr) >= 8);
    for (unsigned i = 0; i < 8; ++i)
        pHlp->pfnPrintf(pHlp, " SR%02X:%02X", i, pThis->sr[i]);
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, Dumps VGA CRTC registers.}
 */
static DECLCALLBACK(void) vgaR3InfoCR(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    unsigned    i;
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "VGA CRTC (3D5): CRTC index 3D4:%02X\n", pThis->cr_index);
    Assert(sizeof(pThis->cr) >= 24);
    for (i = 0; i < 10; ++i)
        pHlp->pfnPrintf(pHlp, " CR%02X:%02X", i, pThis->cr[i]);
    pHlp->pfnPrintf(pHlp, "\n");
    for (i = 10; i < 20; ++i)
        pHlp->pfnPrintf(pHlp, " CR%02X:%02X", i, pThis->cr[i]);
    pHlp->pfnPrintf(pHlp, "\n");
    for (i = 20; i < 25; ++i)
        pHlp->pfnPrintf(pHlp, " CR%02X:%02X", i, pThis->cr[i]);
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV,
 *      Dumps VGA Graphics Controller registers.}
 */
static DECLCALLBACK(void) vgaR3InfoGR(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "VGA Graphics Controller (3CF): GR index 3CE:%02X\n", pThis->gr_index);
    Assert(sizeof(pThis->gr) >= 9);
    for (unsigned i = 0; i < 9; ++i)
        pHlp->pfnPrintf(pHlp, " GR%02X:%02X", i, pThis->gr[i]);
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV,
 *      Dumps VGA Attribute Controller registers.}
 */
static DECLCALLBACK(void) vgaR3InfoAR(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    unsigned    i;
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "VGA Attribute Controller (3C0): index reg %02X, flip-flop: %d (%s)\n",
                    pThis->ar_index, pThis->ar_flip_flop, pThis->ar_flip_flop ? "data" : "index" );
    Assert(sizeof(pThis->ar) >= 0x14);
    pHlp->pfnPrintf(pHlp, " Palette:");
    for (i = 0; i < 0x10; ++i)
        pHlp->pfnPrintf(pHlp, " %02X", pThis->ar[i]);
    pHlp->pfnPrintf(pHlp, "\n");
    for (i = 0x10; i <= 0x14; ++i)
        pHlp->pfnPrintf(pHlp, " AR%02X:%02X", i, pThis->ar[i]);
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, Dumps VGA DAC registers.}
 */
static DECLCALLBACK(void) vgaR3InfoDAC(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "VGA DAC contents:\n");
    for (unsigned i = 0; i < 0x100; ++i)
        pHlp->pfnPrintf(pHlp, " %02X: %02X %02X %02X\n",
                        i, pThis->palette[i*3+0], pThis->palette[i*3+1], pThis->palette[i*3+2]);
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, Dumps VBE registers.}
 */
static DECLCALLBACK(void) vgaR3InfoVBE(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "LFB at %RGp\n", pThis->GCPhysVRAM);
    if (!(pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED))
        pHlp->pfnPrintf(pHlp, "VBE disabled\n");
    else
    {
        pHlp->pfnPrintf(pHlp, "VBE state (chip ID 0x%04x):\n", pThis->vbe_regs[VBE_DISPI_INDEX_ID]);
        pHlp->pfnPrintf(pHlp, " Display resolution: %d x %d @ %dbpp\n",
                        pThis->vbe_regs[VBE_DISPI_INDEX_XRES], pThis->vbe_regs[VBE_DISPI_INDEX_YRES],
                        pThis->vbe_regs[VBE_DISPI_INDEX_BPP]);
        pHlp->pfnPrintf(pHlp, " Virtual resolution: %d x %d\n",
                        pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH], pThis->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT]);
        pHlp->pfnPrintf(pHlp, " Display start addr: %d, %d\n",
                        pThis->vbe_regs[VBE_DISPI_INDEX_X_OFFSET], pThis->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET]);
        pHlp->pfnPrintf(pHlp, " Linear scanline pitch: 0x%04x\n", pThis->vbe_line_offset);
        pHlp->pfnPrintf(pHlp, " Linear display start : 0x%04x\n", pThis->vbe_start_addr);
        pHlp->pfnPrintf(pHlp, " Selected bank: 0x%04x\n", pThis->vbe_regs[VBE_DISPI_INDEX_BANK]);
        pHlp->pfnPrintf(pHlp, " DAC: %d-bit\n", pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_8BIT_DAC ? 8 : 6);
    }
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV,
 *      Dumps register state relevant to 16-color planar graphics modes (GR/SR)
 *      in human-readable form.}
 */
static DECLCALLBACK(void) vgaR3InfoPlanar(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    NOREF(pszArgs);

    unsigned val1 = (pThis->gr[5] >> 3) & 1;
    unsigned val2 = pThis->gr[5] & 3;
    pHlp->pfnPrintf(pHlp, "read mode     : %u     write mode: %u\n", val1, val2);
    val1 = pThis->gr[0];
    val2 = pThis->gr[1];
    pHlp->pfnPrintf(pHlp, "set/reset data: %02X    S/R enable: %02X\n", val1, val2);
    val1 = pThis->gr[2];
    val2 = pThis->gr[4] & 3;
    pHlp->pfnPrintf(pHlp, "color compare : %02X    read map  : %u\n", val1, val2);
    val1 = pThis->gr[3] & 7;
    val2 = (pThis->gr[3] >> 3) & 3;
    pHlp->pfnPrintf(pHlp, "rotate        : %u     function  : %u\n", val1, val2);
    val1 = pThis->gr[7];
    val2 = pThis->gr[8];
    pHlp->pfnPrintf(pHlp, "don't care    : %02X    bit mask  : %02X\n", val1, val2);
    val1 = pThis->sr[2];
    val2 = pThis->sr[4] & 8;
    pHlp->pfnPrintf(pHlp, "seq plane mask: %02X    chain-4   : %s\n", val1, val2 ? "on" : "off");
}


/* -=-=-=-=-=- Ring 3: IBase -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) vgaR3PortQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIDISPLAYPORT, &pThisCC->IPort);
    /* pThisCC->IVBVACallbacks not used currently. */
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pThisCC->ILeds);
    return NULL;
}


/* -=-=-=-=-=- Ring 3: ILeds -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMILEDPORTS,pfnQueryStatusLed}
 */
static DECLCALLBACK(int) vgaR3PortQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, ILeds);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    switch (iLUN)
    {
        /* LUN #0 is the only one for which we have a status LED. */
        case 0:
        {
            *ppLed = &pThis->Led3D;
            Assert((*ppLed)->u32Magic == PDMLED_MAGIC);
            return VINF_SUCCESS;
        }

        default:
            AssertMsgFailed(("Invalid LUN #%u\n", iLUN));
            return VERR_PDM_NO_SUCH_LUN;
    }
}


/* -=-=-=-=-=- Ring 3: Dummy IDisplayConnector -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIDISPLAYCONNECTOR,pfnResize}
 */
static DECLCALLBACK(int) vgaR3DummyResize(PPDMIDISPLAYCONNECTOR pInterface, uint32_t cBits, void *pvVRAM,
                                          uint32_t cbLine, uint32_t cx, uint32_t cy)
{
    RT_NOREF(pInterface, cBits, pvVRAM, cbLine, cx, cy);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIDISPLAYCONNECTOR,pfnUpdateRect}
 */
static DECLCALLBACK(void) vgaR3DummyUpdateRect(PPDMIDISPLAYCONNECTOR pInterface, uint32_t x, uint32_t y, uint32_t cx, uint32_t cy)
{
    RT_NOREF(pInterface, x, y, cx, cy);
}


/**
 * @interface_method_impl{PDMIDISPLAYCONNECTOR,pfnRefresh}
 */
static DECLCALLBACK(void) vgaR3DummyRefresh(PPDMIDISPLAYCONNECTOR pInterface)
{
    NOREF(pInterface);
}


/* -=-=-=-=-=- Ring 3: IDisplayPort -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnUpdateDisplay}
 */
static DECLCALLBACK(int) vgaR3PortUpdateDisplay(PPDMIDISPLAYPORT pInterface)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRCReturn(rc, rc);

# ifdef VBOX_WITH_VMSVGA
    if (    pThis->svga.fEnabled
        &&  !pThis->svga.fTraces)
    {
        /* Nothing to do as the guest will explicitely update us about frame buffer changes. */
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        return VINF_SUCCESS;
    }
#endif

# ifndef VBOX_WITH_HGSMI
    /* This should be called only in non VBVA mode. */
# else
    if (VBVAUpdateDisplay(pThis, pThisCC) == VINF_SUCCESS)
    {
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        return VINF_SUCCESS;
    }
# endif /* VBOX_WITH_HGSMI */

    STAM_COUNTER_INC(&pThis->StatUpdateDisp);

    if (pThis->GCPhysVRAM != 0 && pThis->GCPhysVRAM != NIL_RTGCPHYS)
        vgaR3UpdateDirtyBitsAndResetMonitoring(pDevIns, pThis);

    if (pThis->bmPageRemappedVGA != 0)
    {
        PDMDevHlpMmioResetRegion(pDevIns, pThis->hMmioLegacy);
        STAM_COUNTER_INC(&pThis->StatMapReset);
        vgaResetRemapped(pThis);
    }

    rc = vgaR3UpdateDisplay(pDevIns, pThis, pThisCC, false /*fUpdateAll*/, false /*fFailOnResize*/, true /*reset_dirty*/,
                            pThisCC->pDrv, &pThis->graphic_mode);
    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}


/**
 * Internal vgaR3PortUpdateDisplayAll worker called under pThis->CritSect.
 */
static int vgaR3UpdateDisplayAllInternal(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, bool fFailOnResize)
{
# ifdef VBOX_WITH_VMSVGA
    if (   !pThis->svga.fEnabled
        || pThis->svga.fTraces)
# endif
    {
        /* Update the dirty bits. */
        if (pThis->GCPhysVRAM != 0 && pThis->GCPhysVRAM != NIL_RTGCPHYS)
            vgaR3UpdateDirtyBitsAndResetMonitoring(pDevIns, pThis);
    }

    if (pThis->bmPageRemappedVGA != 0)
    {
        PDMDevHlpMmioResetRegion(pDevIns, pThis->hMmioLegacy);
        STAM_COUNTER_INC(&pThis->StatMapReset);
        vgaResetRemapped(pThis);
    }

    pThis->graphic_mode = -1; /* force full update */

    return vgaR3UpdateDisplay(pDevIns, pThis, pThisCC, true /*fUpdateAll*/, fFailOnResize,
                              true /*reset_dirty*/, pThisCC->pDrv, &pThis->graphic_mode);
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnUpdateDisplayAll}
 */
static DECLCALLBACK(int) vgaR3PortUpdateDisplayAll(PPDMIDISPLAYPORT pInterface, bool fFailOnResize)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);

    /* This is called both in VBVA mode and normal modes. */

# ifdef DEBUG_sunlover
    LogFlow(("vgaR3PortUpdateDisplayAll\n"));
# endif /* DEBUG_sunlover */

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRCReturn(rc, rc);

    rc = vgaR3UpdateDisplayAllInternal(pDevIns, pThis, pThisCC, fFailOnResize);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnSetRefreshRate}
 */
static DECLCALLBACK(int) vgaR3PortSetRefreshRate(PPDMIDISPLAYPORT pInterface, uint32_t cMilliesInterval)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);

    /*
     * Update the interval, notify the VMSVGA FIFO thread if sleeping,
     * then restart or stop the timer.
     */
    ASMAtomicWriteU32(&pThis->cMilliesRefreshInterval, cMilliesInterval);

# ifdef VBOX_WITH_VMSVGA
    if (pThis->svga.fFIFOThreadSleeping)
        PDMDevHlpSUPSemEventSignal(pDevIns, pThis->svga.hFIFORequestSem);
# endif

    if (cMilliesInterval)
        return PDMDevHlpTimerSetMillies(pDevIns, pThis->hRefreshTimer, cMilliesInterval);
    return PDMDevHlpTimerStop(pDevIns, pThis->hRefreshTimer);
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnQueryVideoMode}
 */
static DECLCALLBACK(int) vgaR3PortQueryVideoMode(PPDMIDISPLAYPORT pInterface, uint32_t *pcBits, uint32_t *pcx, uint32_t *pcy)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);

    AssertReturn(pcBits, VERR_INVALID_PARAMETER);

    *pcBits = vgaR3GetBpp(pThis);
    if (pcx)
        *pcx = pThis->last_scr_width;
    if (pcy)
        *pcy = pThis->last_scr_height;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnTakeScreenshot}
 */
static DECLCALLBACK(int) vgaR3PortTakeScreenshot(PPDMIDISPLAYPORT pInterface, uint8_t **ppbData, size_t *pcbData,
                                                 uint32_t *pcx, uint32_t *pcy)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PDMDEV_ASSERT_EMT(pDevIns);

    LogFlow(("vgaR3PortTakeScreenshot: ppbData=%p pcbData=%p pcx=%p pcy=%p\n", ppbData, pcbData, pcx, pcy));

    /*
     * Validate input.
     */
    if (!RT_VALID_PTR(ppbData) || !RT_VALID_PTR(pcbData) || !RT_VALID_PTR(pcx) || !RT_VALID_PTR(pcy))
        return VERR_INVALID_PARAMETER;

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRCReturn(rc, rc);

    /*
     * Get screenshot. This function will fail if a resize is required.
     * So there is not need to do a 'vgaR3UpdateDisplayAllInternal' before taking screenshot.
     */

    /*
     * Allocate the buffer for 32 bits per pixel bitmap
     *
     * Note! The size can't be zero or greater than the size of the VRAM.
     *       Inconsistent VGA device state can cause the incorrect size values.
     */
    size_t cbRequired = pThis->last_scr_width * 4 * pThis->last_scr_height;
    if (cbRequired && cbRequired <= pThis->vram_size)
    {
        uint8_t *pbData = (uint8_t *)RTMemAlloc(cbRequired);
        if (pbData != NULL)
        {
            /*
             * Only 3 methods, assigned below, will be called during the screenshot update.
             * All other are already set to NULL.
             */
            /* The display connector interface is temporarily replaced with the fake one. */
            PDMIDISPLAYCONNECTOR Connector;
            RT_ZERO(Connector);
            Connector.pbData        = pbData;
            Connector.cBits         = 32;
            Connector.cx            = pThis->last_scr_width;
            Connector.cy            = pThis->last_scr_height;
            Connector.cbScanline    = Connector.cx * 4;
            Connector.pfnRefresh    = vgaR3DummyRefresh;
            Connector.pfnResize     = vgaR3DummyResize;
            Connector.pfnUpdateRect = vgaR3DummyUpdateRect;

            int32_t cur_graphic_mode = -1;

            bool fSavedRenderVRAM = pThis->fRenderVRAM;
            pThis->fRenderVRAM = true;

            /*
             * Take the screenshot.
             *
             * The second parameter is 'false' because the current display state is being rendered to an
             * external buffer using a fake connector. That is if display is blanked, we expect a black
             * screen in the external buffer.
             * If there is a pending resize, the function will fail.
             */
            rc = vgaR3UpdateDisplay(pDevIns, pThis, pThisCC, false /*fUpdateAll*/, true /*fFailOnResize*/,
                                    false /*reset_dirty*/, &Connector, &cur_graphic_mode);

            pThis->fRenderVRAM = fSavedRenderVRAM;

            if (rc == VINF_SUCCESS)
            {
                /*
                 * Return the result.
                 */
                *ppbData = pbData;
                *pcbData = cbRequired;
                *pcx = Connector.cx;
                *pcy = Connector.cy;
            }
            else
            {
                /* If we do not return a success, then the data buffer must be freed. */
                RTMemFree(pbData);
                if (RT_SUCCESS_NP(rc))
                {
                    AssertMsgFailed(("%Rrc\n", rc));
                    rc = VERR_INTERNAL_ERROR_5;
                }
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_NOT_SUPPORTED;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);

    LogFlow(("vgaR3PortTakeScreenshot: returns %Rrc (cbData=%d cx=%d cy=%d)\n", rc, *pcbData, *pcx, *pcy));
    return rc;
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnFreeScreenshot}
 */
static DECLCALLBACK(void) vgaR3PortFreeScreenshot(PPDMIDISPLAYPORT pInterface, uint8_t *pbData)
{
    NOREF(pInterface);

    LogFlow(("vgaR3PortFreeScreenshot: pbData=%p\n", pbData));

    RTMemFree(pbData);
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnDisplayBlt}
 */
static DECLCALLBACK(int) vgaR3PortDisplayBlt(PPDMIDISPLAYPORT pInterface, const void *pvData,
                                             uint32_t x, uint32_t y, uint32_t cx, uint32_t cy)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PDMDEV_ASSERT_EMT(pDevIns);
    LogFlow(("vgaR3PortDisplayBlt: pvData=%p x=%d y=%d cx=%d cy=%d\n", pvData, x, y, cx, cy));

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRCReturn(rc, rc);

    /*
     * Validate input.
     */
    if (    pvData
        &&  x      <  pThisCC->pDrv->cx
        &&  cx     <= pThisCC->pDrv->cx
        &&  cx + x <= pThisCC->pDrv->cx
        &&  y      <  pThisCC->pDrv->cy
        &&  cy     <= pThisCC->pDrv->cy
        &&  cy + y <= pThisCC->pDrv->cy)
    {
        /*
         * Determine bytes per pixel in the destination buffer.
         */
        size_t  cbPixelDst = 0;
        switch (pThisCC->pDrv->cBits)
        {
            case 8:
                cbPixelDst = 1;
                break;
            case 15:
            case 16:
                cbPixelDst = 2;
                break;
            case 24:
                cbPixelDst = 3;
                break;
            case 32:
                cbPixelDst = 4;
                break;
            default:
                rc = VERR_INVALID_PARAMETER;
                break;
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * The blitting loop.
             */
            size_t      cbLineSrc   = cx * 4; /* 32 bits per pixel. */
            uint8_t    *pbSrc       = (uint8_t *)pvData;
            size_t      cbLineDst   = pThisCC->pDrv->cbScanline;
            uint8_t    *pbDst       = pThisCC->pDrv->pbData + y * cbLineDst + x * cbPixelDst;
            uint32_t    cyLeft      = cy;
            vga_draw_line_func *pfnVgaDrawLine = vga_draw_line_table[VGA_DRAW_LINE32 * 4 + vgaR3GetDepthIndex(pThisCC->pDrv->cBits)];
            Assert(pfnVgaDrawLine);
            while (cyLeft-- > 0)
            {
                pfnVgaDrawLine(pThis, pThisCC, pbDst, pbSrc, cx);
                pbDst += cbLineDst;
                pbSrc += cbLineSrc;
            }

            /*
             * Invalidate the area.
             */
            pThisCC->pDrv->pfnUpdateRect(pThisCC->pDrv, x, y, cx, cy);
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);

    LogFlow(("vgaR3PortDisplayBlt: returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnUpdateDisplayRect}
 */
static DECLCALLBACK(void) vgaR3PortUpdateDisplayRect(PPDMIDISPLAYPORT pInterface, int32_t x, int32_t y, uint32_t cx, uint32_t cy)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    uint32_t v;

    uint32_t cbPixelDst;
    uint32_t cbLineDst;
    uint8_t *pbDst;

    uint32_t cbPixelSrc;
    uint32_t cbLineSrc;
    uint8_t *pbSrc;


# ifdef DEBUG_sunlover
    LogFlow(("vgaR3PortUpdateDisplayRect: %d,%d %dx%d\n", x, y, cx, cy));
# endif /* DEBUG_sunlover */

    Assert(pInterface);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rc);

    /* Check if there is something to do at all. */
    if (!pThis->fRenderVRAM)
    {
        /* The framebuffer uses the guest VRAM directly. */
# ifdef DEBUG_sunlover
        LogFlow(("vgaR3PortUpdateDisplayRect: nothing to do fRender is false.\n"));
# endif /* DEBUG_sunlover */
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        return;
    }

    Assert(pThisCC->pDrv);
    Assert(pThisCC->pDrv->pbData);

    /* Correct negative x and y coordinates. */
    if (x < 0)
    {
        x += cx; /* Compute xRight which is also the new width. */
        cx = (x < 0) ? 0 : x;
        x = 0;
    }

    if (y < 0)
    {
        y += cy; /* Compute yBottom, which is also the new height. */
        cy = (y < 0) ? 0 : y;
        y = 0;
    }

    /* Also check if coords are greater than the display resolution. */
    if (x + cx > pThisCC->pDrv->cx)
    {
        // x < 0 is not possible here
        cx = pThisCC->pDrv->cx > (uint32_t)x? pThisCC->pDrv->cx - x: 0;
    }

    if (y + cy > pThisCC->pDrv->cy)
    {
        // y < 0 is not possible here
        cy = pThisCC->pDrv->cy > (uint32_t)y? pThisCC->pDrv->cy - y: 0;
    }

# ifdef DEBUG_sunlover
    LogFlow(("vgaR3PortUpdateDisplayRect: %d,%d %dx%d (corrected coords)\n", x, y, cx, cy));
# endif

    /* Check if there is something to do at all. */
    if (cx == 0 || cy == 0)
    {
        /* Empty rectangle. */
# ifdef DEBUG_sunlover
        LogFlow(("vgaR3PortUpdateDisplayRect: nothing to do: %dx%d\n", cx, cy));
#endif
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        return;
    }

    /** @todo This method should be made universal and not only for VBVA.
     *  VGA_DRAW_LINE* must be selected and src/dst address calculation
     *  changed.
     */

    /* Choose the rendering function. */
    switch(pThisCC->get_bpp(pThis))
    {
        default:
        case 0:
            /* A LFB mode is already disabled, but the callback is still called
             * by Display because VBVA buffer is being flushed.
             * Nothing to do, just return.
             */
            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
            return;
        case 8:
            v = VGA_DRAW_LINE8;
            break;
        case 15:
            v = VGA_DRAW_LINE15;
            break;
        case 16:
            v = VGA_DRAW_LINE16;
            break;
        case 24:
            v = VGA_DRAW_LINE24;
            break;
        case 32:
            v = VGA_DRAW_LINE32;
            break;
    }

    vga_draw_line_func *pfnVgaDrawLine = vga_draw_line_table[v * 4 + vgaR3GetDepthIndex(pThisCC->pDrv->cBits)];

    /* Compute source and destination addresses and pitches. */
    cbPixelDst = (pThisCC->pDrv->cBits + 7) / 8;
    cbLineDst  = pThisCC->pDrv->cbScanline;
    pbDst      = pThisCC->pDrv->pbData + y * cbLineDst + x * cbPixelDst;

    cbPixelSrc = (pThisCC->get_bpp(pThis) + 7) / 8;
    uint32_t offSrc, u32Dummy;
    pThisCC->get_offsets(pThis, &cbLineSrc, &offSrc, &u32Dummy);

    /* Assume that rendering is performed only on visible part of VRAM.
     * This is true because coordinates were verified.
     */
    pbSrc = pThisCC->pbVRam;
    pbSrc += offSrc * 4 + y * cbLineSrc + x * cbPixelSrc;

    /* Render VRAM to framebuffer. */

# ifdef DEBUG_sunlover
    LogFlow(("vgaR3PortUpdateDisplayRect: dst: %p, %d, %d. src: %p, %d, %d\n", pbDst, cbLineDst, cbPixelDst, pbSrc, cbLineSrc, cbPixelSrc));
# endif

    while (cy-- > 0)
    {
        pfnVgaDrawLine(pThis, pThisCC, pbDst, pbSrc, cx);
        pbDst += cbLineDst;
        pbSrc += cbLineSrc;
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
# ifdef DEBUG_sunlover
    LogFlow(("vgaR3PortUpdateDisplayRect: completed.\n"));
# endif
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnCopyRect}
 */
static DECLCALLBACK(int)
vgaR3PortCopyRect(PPDMIDISPLAYPORT pInterface,
                  uint32_t cx, uint32_t cy,
                  const uint8_t *pbSrc, int32_t xSrc, int32_t ySrc, uint32_t cxSrc, uint32_t cySrc,
                  uint32_t cbSrcLine, uint32_t cSrcBitsPerPixel,
                  uint8_t *pbDst, int32_t xDst, int32_t yDst, uint32_t cxDst, uint32_t cyDst,
                  uint32_t cbDstLine, uint32_t cDstBitsPerPixel)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    uint32_t v;

# ifdef DEBUG_sunlover
    LogFlow(("vgaR3PortCopyRect: %d,%d %dx%d -> %d,%d\n", xSrc, ySrc, cx, cy, xDst, yDst));
# endif

    Assert(pInterface);
    Assert(pThisCC->pDrv);

    int32_t  xSrcCorrected = xSrc;
    int32_t  ySrcCorrected = ySrc;
    uint32_t cxCorrected = cx;
    uint32_t cyCorrected = cy;

    /* Correct source coordinates to be within the source bitmap. */
    if (xSrcCorrected < 0)
    {
        xSrcCorrected += cxCorrected; /* Compute xRight which is also the new width. */
        cxCorrected = (xSrcCorrected < 0) ? 0 : xSrcCorrected;
        xSrcCorrected = 0;
    }

    if (ySrcCorrected < 0)
    {
        ySrcCorrected += cyCorrected; /* Compute yBottom, which is also the new height. */
        cyCorrected = (ySrcCorrected < 0) ? 0 : ySrcCorrected;
        ySrcCorrected = 0;
    }

    /* Also check if coords are greater than the display resolution. */
    if (xSrcCorrected + cxCorrected > cxSrc)
    {
        /* xSrcCorrected < 0 is not possible here */
        cxCorrected = cxSrc > (uint32_t)xSrcCorrected ? cxSrc - xSrcCorrected : 0;
    }

    if (ySrcCorrected + cyCorrected > cySrc)
    {
        /* y < 0 is not possible here */
        cyCorrected = cySrc > (uint32_t)ySrcCorrected ? cySrc - ySrcCorrected : 0;
    }

# ifdef DEBUG_sunlover
    LogFlow(("vgaR3PortCopyRect: %d,%d %dx%d (corrected coords)\n", xSrcCorrected, ySrcCorrected, cxCorrected, cyCorrected));
# endif

    /* Check if there is something to do at all. */
    if (cxCorrected == 0 || cyCorrected == 0)
    {
        /* Empty rectangle. */
# ifdef DEBUG_sunlover
        LogFlow(("vgaPortUpdateDisplayRectEx: nothing to do: %dx%d\n", cxCorrected, cyCorrected));
# endif
        return VINF_SUCCESS;
    }

    /* Check that the corrected source rectangle is within the destination.
     * Note: source rectangle is adjusted, but the target must be large enough.
     */
    if (   xDst < 0
        || yDst < 0
        || xDst + cxCorrected > cxDst
        || yDst + cyCorrected > cyDst)
    {
        return VERR_INVALID_PARAMETER;
    }

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRCReturn(rc, rc);

    /* This method only works if the VGA device is in a VBE mode or not paused VBVA mode.
     * VGA modes are reported to the caller by returning VERR_INVALID_STATE.
     *
     * If VBE_DISPI_ENABLED is set, then it is a VBE or VBE compatible VBVA mode. Both of them can be handled.
     *
     * If VBE_DISPI_ENABLED is clear, then it is either a VGA mode or a VBVA mode set by guest additions
     * which have VBVACAPS_USE_VBVA_ONLY capability.
     * When VBE_DISPI_ENABLED is being cleared and VBVACAPS_USE_VBVA_ONLY is not set (i.e. guest wants a VGA mode),
     * then VBVAOnVBEChanged makes sure that VBVA is paused.
     * That is a not paused VBVA means that the video mode can be handled even if VBE_DISPI_ENABLED is clear.
     */
    if (   (pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) == 0
        && VBVAIsPaused(pThisCC)
# ifdef VBOX_WITH_VMSVGA
        && !pThis->svga.fEnabled
# endif
       )
    {
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        return VERR_INVALID_STATE;
    }

    /* Choose the rendering function. */
    switch (cSrcBitsPerPixel)
    {
        default:
        case 0:
            /* Nothing to do, just return. */
            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
            return VINF_SUCCESS;
        case 8:
            v = VGA_DRAW_LINE8;
            break;
        case 15:
            v = VGA_DRAW_LINE15;
            break;
        case 16:
            v = VGA_DRAW_LINE16;
            break;
        case 24:
            v = VGA_DRAW_LINE24;
            break;
        case 32:
            v = VGA_DRAW_LINE32;
            break;
    }

    vga_draw_line_func *pfnVgaDrawLine = vga_draw_line_table[v * 4 + vgaR3GetDepthIndex(cDstBitsPerPixel)];

    /* Compute source and destination addresses and pitches. */
    uint32_t cbPixelDst = (cDstBitsPerPixel + 7) / 8;
    uint32_t cbLineDst  = cbDstLine;
    uint8_t *pbDstCur   = pbDst + yDst * cbLineDst + xDst * cbPixelDst;

    uint32_t cbPixelSrc = (cSrcBitsPerPixel + 7) / 8;
    uint32_t cbLineSrc  = cbSrcLine;
    const uint8_t *pbSrcCur = pbSrc + ySrcCorrected * cbLineSrc + xSrcCorrected * cbPixelSrc;

# ifdef DEBUG_sunlover
    LogFlow(("vgaR3PortCopyRect: dst: %p, %d, %d. src: %p, %d, %d\n", pbDstCur, cbLineDst, cbPixelDst, pbSrcCur, cbLineSrc, cbPixelSrc));
# endif

    while (cyCorrected-- > 0)
    {
        pfnVgaDrawLine(pThis, pThisCC, pbDstCur, pbSrcCur, cxCorrected);
        pbDstCur += cbLineDst;
        pbSrcCur += cbLineSrc;
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
# ifdef DEBUG_sunlover
    LogFlow(("vgaR3PortCopyRect: completed.\n"));
# endif
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnSetRenderVRAM}
 */
static DECLCALLBACK(void) vgaR3PortSetRenderVRAM(PPDMIDISPLAYPORT pInterface, bool fRender)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);

    LogFlow(("vgaR3PortSetRenderVRAM: fRender = %d\n", fRender));

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    pThis->fRenderVRAM = fRender;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnReportHostCursorCapabilities}
 */
static DECLCALLBACK(void) vgaR3PortReportHostCursorCapabilities(PPDMIDISPLAYPORT pInterface, bool fSupportsRenderCursor,
                                                                bool fSupportsMoveCursor)
{
    RT_NOREF(pInterface, fSupportsRenderCursor, fSupportsMoveCursor);
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnReportHostCursorPosition}
 */
static DECLCALLBACK(void) vgaR3PortReportHostCursorPosition(PPDMIDISPLAYPORT pInterface, uint32_t x, uint32_t y, bool fOutOfRange)
{
    RT_NOREF(pInterface, x, y, fOutOfRange);
}


/**
 * @callback_method_impl{FNTMTIMERDEV, VGA Refresh Timer}
 */
static DECLCALLBACK(void) vgaR3TimerRefresh(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    RT_NOREF(pvUser);

    if (pThis->fScanLineCfg & VBVASCANLINECFG_ENABLE_VSYNC_IRQ)
        VBVARaiseIrq(pDevIns, pThis, pThisCC, HGSMIHOSTFLAGS_VSYNC);

    if (pThisCC->pDrv)
        pThisCC->pDrv->pfnRefresh(pThisCC->pDrv);

    if (pThis->cMilliesRefreshInterval)
        PDMDevHlpTimerSetMillies(pDevIns, hTimer, pThis->cMilliesRefreshInterval);

# ifdef VBOX_WITH_VMSVGA
    /*
     * Call the VMSVGA FIFO poller/watchdog so we can wake up the thread if
     * there is work to be done.
     */
    if (pThis->svga.fFIFOThreadSleeping && pThis->svga.fEnabled && pThis->svga.fConfigured)
        vmsvgaR3FifoWatchdogTimer(pDevIns, pThis, pThisCC);
# endif
}

# ifdef VBOX_WITH_VMSVGA

/**
 * Helper for VMSVGA.
 */
int vgaR3RegisterVRAMHandler(PPDMDEVINS pDevIns, PVGASTATE pThis, uint64_t cbFrameBuffer)
{
    Assert(pThis->GCPhysVRAM != 0 && pThis->GCPhysVRAM != NIL_RTGCPHYS);
    int rc = PDMDevHlpMmio2ControlDirtyPageTracking(pDevIns, pThis->hMmio2VRam, true /*fEnabled*/);
    RT_NOREF(cbFrameBuffer);
    AssertRC(rc);
    return rc;
}


/**
 * Helper for VMSVGA.
 */
int vgaR3UnregisterVRAMHandler(PPDMDEVINS pDevIns, PVGASTATE pThis)
{
    Assert(pThis->GCPhysVRAM != 0 && pThis->GCPhysVRAM != NIL_RTGCPHYS);
    int rc = PDMDevHlpMmio2ControlDirtyPageTracking(pDevIns, pThis->hMmio2VRam, false /*fEnabled*/);
    AssertRC(rc);
    return rc;
}

# endif /* VBOX_WITH_VMSVGA */


/* -=-=-=-=-=- Ring 3: PCI Device -=-=-=-=-=- */

/**
 * @callback_method_impl{FNPCIIOREGIONMAP, Mapping/unmapping the VRAM MMI2 region}
 */
static DECLCALLBACK(int) vgaR3PciIORegionVRamMapUnmap(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                      RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    Log(("vgaR3PciIORegionVRamMapUnmap: iRegion=%d GCPhysAddress=%RGp cb=%RGp enmType=%d\n", iRegion, GCPhysAddress, cb, enmType));
    RT_NOREF(pPciDev, cb);

# ifdef VBOX_WITH_VMSVGA
    AssertReturn(   iRegion == pThis->pciRegions.iVRAM
                 && (    enmType == PCI_ADDRESS_SPACE_MEM_PREFETCH
                     || (enmType == PCI_ADDRESS_SPACE_MEM && pThis->fVMSVGAEnabled && pThis->fStateLoaded)), VERR_INTERNAL_ERROR);
# else
    AssertReturn(   iRegion == pThis->pciRegions.iVRAM
                 && enmType == PCI_ADDRESS_SPACE_MEM_PREFETCH, VERR_INTERNAL_ERROR);
# endif

    Assert(pPciDev == pDevIns->apPciDevs[0]);

    /* Note! We cannot take the device lock here as that would create a lock order
             problem as the caller has taken the PDM lock prior to calling us.  If
             we did, we will get trouble later when raising interrupts while owning
             the device lock (e.g. vmsvgaR3FifoLoop). */

    int rc;
    if (GCPhysAddress != NIL_RTGCPHYS)
    {
        /*
         * Make sure the dirty page tracking state is up to date before mapping it.
         */
# ifdef VBOX_WITH_VMSVGA
        rc = PDMDevHlpMmio2ControlDirtyPageTracking(pDevIns, pThis->hMmio2VRam, !pThis->svga.fEnabled || pThis->svga.fVRAMTracking);
# else
        rc = PDMDevHlpMmio2ControlDirtyPageTracking(pDevIns, pThis->hMmio2VRam, true /*fEnabled*/);
# endif
        AssertLogRelRC(rc);

        /*
         * Map the VRAM.
         */
        rc = PDMDevHlpMmio2Map(pDevIns, pThis->hMmio2VRam, GCPhysAddress);
        AssertLogRelRC(rc);
        if (RT_SUCCESS(rc))
        {
            pThis->GCPhysVRAM = GCPhysAddress;
            pThis->vbe_regs[VBE_DISPI_INDEX_FB_BASE_HI] = GCPhysAddress >> 16;

            rc = VINF_PCI_MAPPING_DONE; /* caller doesn't care about any other status, so no problem overwriting error here */
        }
    }
    else
    {
        /*
         * Unmapping of the VRAM in progress (caller will do that).
         */
        Assert(pThis->GCPhysVRAM);
        pThis->GCPhysVRAM = 0;
        rc = VINF_SUCCESS;
        /* NB: VBE_DISPI_INDEX_FB_BASE_HI is left unchanged here. */
    }
    return rc;
}


# ifdef VBOX_WITH_VMSVGA /* Currently not needed in the non-VMSVGA mode, but keeping it flexible for later. */
/**
 * @interface_method_impl{PDMPCIDEV,pfnRegionLoadChangeHookR3}
 */
static DECLCALLBACK(int) vgaR3PciRegionLoadChangeHook(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                      uint64_t cbRegion, PCIADDRESSSPACE enmType,
                                                      PFNPCIIOREGIONOLDSETTER pfnOldSetter, PFNPCIIOREGIONSWAP pfnSwapRegions)
{
    PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);

#  ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAEnabled)
    {
        /*
         * We messed up BAR order for the hybrid devices in 6.0 (see #9359).
         * It should have been compatible with the VBox VGA device and had the
         * VRAM region first and I/O second, but instead the I/O region ended
         * up first and VRAM second like the VMSVGA device.
         *
         * So, we have to detect that here and reconfigure the memory regions.
         * Region numbers are used in our (and the PCI bus') interfaction with
         * PGM, so PGM needs to be informed too.
         */
        if (   iRegion == 0
            && iRegion == pThis->pciRegions.iVRAM
            && (enmType & PCI_ADDRESS_SPACE_IO))
        {
            LogRel(("VGA: Detected old BAR config, making adjustments.\n"));

            /* Update the entries. */
            pThis->pciRegions.iIO   = 0;
            pThis->pciRegions.iVRAM = 1;

            /* Update PGM on the region number change so it won't barf when restoring state. */
            AssertLogRelReturn(pDevIns->CTX_SUFF(pHlp)->pfnMmio2ChangeRegionNo, VERR_VERSION_MISMATCH);
            int rc = pDevIns->CTX_SUFF(pHlp)->pfnMmio2ChangeRegionNo(pDevIns, pThis->hMmio2VRam, 1);
            AssertLogRelRCReturn(rc, rc);
            /** @todo Update the I/O port too, only currently we don't give a hoot about
             *        the region number in the I/O port registrations so it can wait...
             *        (Only visible in the 'info ioport' output IIRC).   */

            /* Update the calling PCI device. */
            AssertLogRelReturn(pfnSwapRegions, VERR_INTERNAL_ERROR_2);
            rc = pfnSwapRegions(pPciDev, 0, 1);
            AssertLogRelRCReturn(rc, rc);

            return rc;
        }

        /*
         * The VMSVGA changed the default FIFO size from 128KB to 2MB after 5.1.
         */
        if (iRegion == pThis->pciRegions.iFIFO)
        {
            /* Make sure it's still 32-bit memory.  Ignore fluxtuations in the prefetch flag. */
            AssertLogRelMsgReturn(!(enmType & (PCI_ADDRESS_SPACE_IO | PCI_ADDRESS_SPACE_BAR64)), ("enmType=%#x\n", enmType),
                                  VERR_VGA_UNEXPECTED_PCI_REGION_LOAD_CHANGE);

            /* If the size didn't change we're fine, so just return already. */
            if (cbRegion == pThis->svga.cbFIFO)
                return VINF_SUCCESS;

            /* If the size is larger than the current configuration, refuse to load. */
            AssertLogRelMsgReturn(cbRegion <= pThis->svga.cbFIFOConfig,
                                  ("cbRegion=%#RGp cbFIFOConfig=%#x cbFIFO=%#x\n",
                                   cbRegion, pThis->svga.cbFIFOConfig, pThis->svga.cbFIFO),
                                  VERR_SSM_LOAD_CONFIG_MISMATCH);

            /* Adjust the size down. */
            int rc = PDMDevHlpMmio2Reduce(pDevIns, pThis->hMmio2VmSvgaFifo, cbRegion);
            AssertLogRelMsgRCReturn(rc,
                                    ("cbRegion=%#RGp cbFIFOConfig=%#x cbFIFO=%#x: %Rrc\n",
                                     cbRegion, pThis->svga.cbFIFOConfig, pThis->svga.cbFIFO, rc),
                                    rc);
            pThis->svga.cbFIFO = cbRegion;
            return rc;

        }

        /*
         * VRAM used to be non-prefetchable till 6.1.0, so we end up here when restoring
         * states older than that with 6.1.0 and later.  We just have to check that
         * the size and basic type matches, then return VINF_SUCCESS to ACK it.
         */
        if (iRegion == pThis->pciRegions.iVRAM)
        {
            /* Make sure it's still 32-bit memory.  Ignore fluxtuations in the prefetch flag. */
            AssertLogRelMsgReturn(!(enmType & (PCI_ADDRESS_SPACE_IO | PCI_ADDRESS_SPACE_BAR64)), ("enmType=%#x\n", enmType),
                                  VERR_VGA_UNEXPECTED_PCI_REGION_LOAD_CHANGE);
            /* The size must be the same. */
            AssertLogRelMsgReturn(cbRegion == pThis->vram_size,
                                  ("cbRegion=%#RGp vram_size=%#x\n", cbRegion, pThis->vram_size),
                                  VERR_SSM_LOAD_CONFIG_MISMATCH);
            return VINF_SUCCESS;
        }

        /* Emulate callbacks for 5.1 and older saved states by recursion. */
        if (iRegion == UINT32_MAX)
        {
            int rc = vgaR3PciRegionLoadChangeHook(pDevIns, pPciDev, pThis->pciRegions.iFIFO, VMSVGA_FIFO_SIZE_OLD,
                                                  PCI_ADDRESS_SPACE_MEM, NULL, NULL);
            if (RT_SUCCESS(rc))
                rc = pfnOldSetter(pPciDev, pThis->pciRegions.iFIFO, VMSVGA_FIFO_SIZE_OLD, PCI_ADDRESS_SPACE_MEM);
            return rc;
        }
    }
#  endif /* VBOX_WITH_VMSVGA */

    return VERR_VGA_UNEXPECTED_PCI_REGION_LOAD_CHANGE;
}
# endif /* VBOX_WITH_VMSVGA */


/* -=-=-=-=-=- Ring3: Misc Wrappers & Sidekicks -=-=-=-=-=- */

/**
 * Saves a important bits of the VGA device config.
 *
 * @param   pHlp        The device helpers (for SSM functions).
 * @param   pThis       The shared VGA instance data.
 * @param   pSSM        The saved state handle.
 */
static void vgaR3SaveConfig(PCPDMDEVHLPR3 pHlp, PVGASTATE pThis, PSSMHANDLE pSSM)
{
    pHlp->pfnSSMPutU32(pSSM, pThis->vram_size);
    pHlp->pfnSSMPutU32(pSSM, pThis->cMonitors);
}


/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) vgaR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    Assert(uPass == 0); NOREF(uPass);
    vgaR3SaveConfig(pDevIns->pHlpR3, pThis, pSSM);
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEPREP}
 */
static DECLCALLBACK(int) vgaR3SavePrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pDevIns, pSSM);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEDONE}
 */
static DECLCALLBACK(int) vgaR3SaveDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pDevIns, pSSM);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) vgaR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

# ifdef VBOX_WITH_VDMA
    vboxVDMASaveStateExecPrep(pThisCC->pVdma);
# endif

    vgaR3SaveConfig(pHlp, pThis, pSSM);
    vga_save(pHlp, pSSM, PDMDEVINS_2_DATA(pDevIns, PVGASTATE));

    VGA_SAVED_STATE_PUT_MARKER(pSSM, 1);
# ifdef VBOX_WITH_HGSMI
    pHlp->pfnSSMPutBool(pSSM, true);
    int rc = vboxVBVASaveStateExec(pDevIns, pSSM);
# else
    int rc = pHlp->pfnSSMPutBool(pSSM, false);
# endif

    AssertRCReturn(rc, rc);

    VGA_SAVED_STATE_PUT_MARKER(pSSM, 3);
# ifdef VBOX_WITH_VDMA
    rc = pHlp->pfnSSMPutU32(pSSM, 1);
    AssertRCReturn(rc, rc);
    rc = vboxVDMASaveStateExecPerform(pHlp, pThisCC->pVdma, pSSM);
# else
    rc = pHlp->pfnSSMPutU32(pSSM, 0);
# endif
    AssertRCReturn(rc, rc);

# ifdef VBOX_WITH_VDMA
    vboxVDMASaveStateExecDone(pThisCC->pVdma);
# endif

    VGA_SAVED_STATE_PUT_MARKER(pSSM, 5);
# ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAEnabled)
    {
        rc = vmsvgaR3SaveExec(pDevIns, pSSM);
        AssertRCReturn(rc, rc);
    }
# endif
    VGA_SAVED_STATE_PUT_MARKER(pSSM, 6);

    return rc;
}


/**
 * @callback_method_impl{FNSSMDEVLOADPREP}
 */
static DECLCALLBACK(int) vgaR3LoadPrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    RT_NOREF(pSSM);
    pThis->fStateLoaded = true;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) vgaR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    pThis->fStateLoaded = true;

    if (uVersion < VGA_SAVEDSTATE_VERSION_ANCIENT || uVersion > VGA_SAVEDSTATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    if (uVersion > VGA_SAVEDSTATE_VERSION_HGSMI)
    {
        /* Check the config */
        uint32_t cbVRam;
        rc = pHlp->pfnSSMGetU32(pSSM, &cbVRam);
        AssertRCReturn(rc, rc);
        if (pThis->vram_size != cbVRam)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("VRAM size changed: config=%#x state=%#x"), pThis->vram_size, cbVRam);

        uint32_t cMonitors;
        rc = pHlp->pfnSSMGetU32(pSSM, &cMonitors);
        AssertRCReturn(rc, rc);
        if (pThis->cMonitors != cMonitors)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Monitor count changed: config=%u state=%u"), pThis->cMonitors, cMonitors);
    }

    if (uPass == SSM_PASS_FINAL)
    {
        rc = vga_load(pHlp, pSSM, pThis, uVersion);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Restore the HGSMI state, if present.
         */
        VGA_SAVED_STATE_GET_MARKER_RETURN_ON_MISMATCH(pSSM, uVersion, 1);
        bool fWithHgsmi = uVersion == VGA_SAVEDSTATE_VERSION_HGSMI;
        if (uVersion > VGA_SAVEDSTATE_VERSION_HGSMI)
        {
            rc = pHlp->pfnSSMGetBool(pSSM, &fWithHgsmi);
            AssertRCReturn(rc, rc);
        }
        if (fWithHgsmi)
        {
# ifdef VBOX_WITH_HGSMI
            rc = vboxVBVALoadStateExec(pDevIns, pSSM, uVersion);
            AssertRCReturn(rc, rc);
# else
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("HGSMI is not compiled in, but it is present in the saved state"));
# endif
        }

        VGA_SAVED_STATE_GET_MARKER_RETURN_ON_MISMATCH(pSSM, uVersion, 3);
        if (uVersion >= VGA_SAVEDSTATE_VERSION_3D)
        {
            uint32_t u32;
            rc = pHlp->pfnSSMGetU32(pSSM, &u32);
            AssertRCReturn(rc, rc);

            if (u32)
            {
# ifdef VBOX_WITH_VDMA
                if (u32 == 1)
                {
                    rc = vboxVDMASaveLoadExecPerform(pHlp, pThisCC->pVdma, pSSM, uVersion);
                    AssertRCReturn(rc, rc);
                }
                else
# endif
                {
                    LogRel(("invalid CmdVbva version info\n"));
                    return VERR_VERSION_MISMATCH;
                }
            }
        }

        VGA_SAVED_STATE_GET_MARKER_RETURN_ON_MISMATCH(pSSM, uVersion, 5);
# ifdef VBOX_WITH_VMSVGA
        if (pThis->fVMSVGAEnabled)
        {
            rc = vmsvgaR3LoadExec(pDevIns, pSSM, uVersion, uPass);
            AssertRCReturn(rc, rc);
        }
# endif
        VGA_SAVED_STATE_GET_MARKER_RETURN_ON_MISMATCH(pSSM, uVersion, 6);
    }
    return VINF_SUCCESS;
}


/**
 * @@callback_method_impl{FNSSMDEVLOADDONE}
 */
static DECLCALLBACK(int) vgaR3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    int         rc;
    RT_NOREF(pThisCC, pThis, pSSM);

# ifdef VBOX_WITH_HGSMI
    rc = vboxVBVALoadStateDone(pDevIns);
    AssertRCReturn(rc, rc);
# ifdef VBOX_WITH_VDMA
    rc = vboxVDMASaveLoadDone(pThisCC->pVdma);
    AssertRCReturn(rc, rc);
# endif
    /* Now update the current VBVA state which depends on VBE registers. vboxVBVALoadStateDone cleared the state. */
    VBVAOnVBEChanged(pThis, pThisCC);
# endif
# ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAEnabled)
    {
        rc = vmsvgaR3LoadDone(pDevIns);
        AssertRCReturn(rc, rc);
    }
# endif
    return VINF_SUCCESS;
}


/* -=-=-=-=-=- Ring 3: Device callbacks -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMDEVREG,pfnResume}
 */
static DECLCALLBACK(void) vgaR3Resume(PPDMDEVINS pDevIns)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    VBVAOnResume(pDevIns, pThis, pThisCC);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void)  vgaR3Reset(PPDMDEVINS pDevIns)
{
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    char           *pchStart;
    char           *pchEnd;
    LogFlow(("vgaReset\n"));

    if (pThisCC->pVdma)
        vboxVDMAReset(pThisCC->pVdma);

# ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAEnabled)
        vmsvgaR3Reset(pDevIns);
# endif

# ifdef VBOX_WITH_HGSMI
    VBVAReset(pDevIns, pThis, pThisCC);
# endif


    /* Clear the VRAM ourselves. */
    if (pThisCC->pbVRam && pThis->vram_size)
        memset(pThisCC->pbVRam, 0, pThis->vram_size);

    /*
     * Zero most of it.
     *
     * Unlike vga_reset we're leaving out a few members which we believe
     * must remain unchanged....
     */
    /* 1st part. */
    pchStart = (char *)&pThis->latch;
    pchEnd   = (char *)&pThis->invalidated_y_table;
    memset(pchStart, 0, pchEnd - pchStart);

    /* 2nd part. */
    pchStart = (char *)&pThis->last_palette;
    pchEnd   = (char *)&pThis->u32Marker;
    memset(pchStart, 0, pchEnd - pchStart);


    /*
     * Restore and re-init some bits.
     */
    pThisCC->get_bpp        = vgaR3GetBpp;
    pThisCC->get_offsets    = vgaR3GetOffsets;
    pThisCC->get_resolution = vgaR3GetResolution;
    pThis->graphic_mode   = -1;         /* Force full update. */
# ifdef CONFIG_BOCHS_VBE
    pThis->vbe_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID0;
    pThis->vbe_regs[VBE_DISPI_INDEX_VBOX_VIDEO] = 0;
    pThis->vbe_regs[VBE_DISPI_INDEX_FB_BASE_HI] = pThis->GCPhysVRAM >> 16;
    pThis->vbe_bank_max   = (pThis->vram_size >> 16) - 1;
# endif /* CONFIG_BOCHS_VBE */
    pThis->st00 = 0x70; /* Static except for bit 4. */

    /*
     * Reset the LFB mapping.
     */
    if (   (   pDevIns->fRCEnabled
            || pDevIns->fR0Enabled)
        && pThis->GCPhysVRAM != 0
        && pThis->GCPhysVRAM != NIL_RTGCPHYS)
    {
        /** @todo r=bird: This used to be a PDMDevHlpPGMHandlerPhysicalReset call.
         *        Not quite sure if it was/is needed. Besides, where do we reset the
         *        dirty bitmap (bmDirtyBitmap)? */
        int rc = PDMDevHlpMmio2ResetDirtyBitmap(pDevIns, pThis->hMmio2VRam);
        AssertRC(rc);
    }
    if (pThis->bmPageRemappedVGA != 0)
    {
        PDMDevHlpMmioResetRegion(pDevIns, pThis->hMmioLegacy);
        STAM_COUNTER_INC(&pThis->StatMapReset);
        vgaResetRemapped(pThis);
    }

    /*
     * Reset the logo data.
     */
    pThisCC->LogoCommand = LOGO_CMD_NOP;
    pThisCC->offLogoData = 0;

    /* notify port handler */
    if (pThisCC->pDrv)
    {
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect); /* hack around lock order issue. */

        pThisCC->pDrv->pfnReset(pThisCC->pDrv);
        pThisCC->pDrv->pfnVBVAMousePointerShape(pThisCC->pDrv, false, false, 0, 0, 0, 0, NULL);

        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);
    }

    /* Reset latched access mask. */
    pThis->uMaskLatchAccess     = 0x3ff;
    pThis->cLatchAccesses       = 0;
    pThis->u64LastLatchedAccess = 0;
    pThis->iMask                = 0;

    /* Reset retrace emulation. */
    memset(&pThis->retrace_state, 0, sizeof(pThis->retrace_state));
}


/**
 * @interface_method_impl{PDMDEVREG,pfnPowerOn}
 */
static DECLCALLBACK(void) vgaR3PowerOn(PPDMDEVINS pDevIns)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
# ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAEnabled)
        vmsvgaR3PowerOn(pDevIns);
# endif
    VBVAOnResume(pDevIns, pThis, pThisCC);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnPowerOff}
 */
static DECLCALLBACK(void) vgaR3PowerOff(PPDMDEVINS pDevIns)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    RT_NOREF(pThis, pThisCC);
# ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAEnabled)
        vmsvgaR3PowerOff(pDevIns);
# endif
}


/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) vgaR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
# ifdef VBOX_WITH_RAW_MODE_KEEP
    if (offDelta)
    {
        PVGASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
        LogFlow(("vgaRelocate: offDelta = %08X\n", offDelta));

        pThisRC->pbVRam += offDelta;
        pThis->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
    }
# else
    RT_NOREF(pDevIns, offDelta);
# endif
}


/**
 * @interface_method_impl{PDMDEVREG,pfnAttach}
 *
 * This is like plugging in the monitor after turning on the PC.
 */
static DECLCALLBACK(int)  vgaAttach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);

    RT_NOREF(pThis);

    AssertMsgReturn(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
                    ("VGA device does not support hotplugging\n"),
                    VERR_INVALID_PARAMETER);

    switch (iLUN)
    {
        /* LUN #0: Display port. */
        case 0:
        {
            int rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pThisCC->IBase, &pThisCC->pDrvBase, "Display Port");
            if (RT_SUCCESS(rc))
            {
                pThisCC->pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIDISPLAYCONNECTOR);
                if (pThisCC->pDrv)
                {
                    /* pThisCC->pDrv->pbData can be NULL when there is no framebuffer. */
                    if (    pThisCC->pDrv->pfnRefresh
                        &&  pThisCC->pDrv->pfnResize
                        &&  pThisCC->pDrv->pfnUpdateRect)
                        rc = VINF_SUCCESS;
                    else
                    {
                        Assert(pThisCC->pDrv->pfnRefresh);
                        Assert(pThisCC->pDrv->pfnResize);
                        Assert(pThisCC->pDrv->pfnUpdateRect);
                        pThisCC->pDrv = NULL;
                        pThisCC->pDrvBase = NULL;
                        rc = VERR_INTERNAL_ERROR;
                    }
                }
                else
                {
                    AssertMsgFailed(("LUN #0 doesn't have a display connector interface! rc=%Rrc\n", rc));
                    pThisCC->pDrvBase = NULL;
                    rc = VERR_PDM_MISSING_INTERFACE;
                }
            }
            else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
            {
                Log(("%s/%d: warning: no driver attached to LUN #0!\n", pDevIns->pReg->szName, pDevIns->iInstance));
                rc = VINF_SUCCESS;
            }
            else
                AssertLogRelMsgFailed(("Failed to attach LUN #0! rc=%Rrc\n", rc));
            return rc;
        }

        default:
            AssertMsgFailed(("Invalid LUN #%d\n", iLUN));
            return VERR_PDM_NO_SUCH_LUN;
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDetach}
 *
 * This is like unplugging the monitor while the PC is still running.
 */
static DECLCALLBACK(void)  vgaDetach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    AssertMsg(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG, ("VGA device does not support hotplugging\n"));
    RT_NOREF(fFlags);

    /*
     * Reset the interfaces and update the controller state.
     */
    switch (iLUN)
    {
        /* LUN #0: Display port. */
        case 0:
            pThisCC->pDrv = NULL;
            pThisCC->pDrvBase = NULL;
            break;

        default:
            AssertMsgFailed(("Invalid LUN #%d\n", iLUN));
            break;
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) vgaR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    LogFlow(("vgaR3Destruct:\n"));

# ifdef VBOX_WITH_VDMA
    if (pThisCC->pVdma)
        vboxVDMADestruct(pThisCC->pVdma);
# endif

# ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAEnabled)
        vmsvgaR3Destruct(pDevIns);
# endif

# ifdef VBOX_WITH_HGSMI
    VBVADestroy(pThisCC);
# endif

    /*
     * Free MM heap pointers.
     */
    if (pThisCC->pbVBEExtraData)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pbVBEExtraData);
        pThisCC->pbVBEExtraData = NULL;
    }
    if (pThisCC->pbVgaBios)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pbVgaBios);
        pThisCC->pbVgaBios = NULL;
    }

    if (pThisCC->pszVgaBiosFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszVgaBiosFile);
        pThisCC->pszVgaBiosFile = NULL;
    }

    if (pThisCC->pszLogoFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszLogoFile);
        pThisCC->pszLogoFile = NULL;
    }

    if (pThisCC->pbLogo)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pbLogo);
        pThisCC->pbLogo = NULL;
    }

# if defined(VBOX_WITH_VDMA) || defined(VBOX_WITH_WDDM)
    PDMDevHlpCritSectDelete(pDevIns, &pThis->CritSectIRQ);
# endif
    PDMDevHlpCritSectDelete(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * Adjust VBE mode information
 *
 * Depending on the configured VRAM size, certain parts of VBE mode
 * information must be updated.
 *
 * @param   pThis       The device instance data.
 * @param   pMode       The mode information structure.
 */
static void vgaR3AdjustModeInfo(PVGASTATE pThis, ModeInfoListItem *pMode)
{
    /* For 4bpp modes, the planes are "stacked" on top of each other. */
    unsigned bpl = pMode->info.BytesPerScanLine * pMode->info.NumberOfPlanes;
    /* The "number of image pages" is really the max page index... */
    unsigned maxPage = pThis->vram_size / (pMode->info.YResolution * bpl) - 1;
    if (maxPage > 255)
        maxPage = 255;  /* 8-bit value. */
    pMode->info.NumberOfImagePages = maxPage;
    pMode->info.LinNumberOfPages   = maxPage;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)   vgaR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;
    unsigned        i;
    uint32_t        cCustomModes;
    uint32_t        cyReduction;
    uint32_t        cbPitch;
    PVBEHEADER      pVBEDataHdr;
    ModeInfoListItem *pCurMode;
    unsigned        cb;

    Assert(iInstance == 0);

    /*
     * Init static data.
     */
    static bool s_fExpandDone = false;
    if (!s_fExpandDone)
    {
        s_fExpandDone = true;
        vgaR3InitExpand();
    }

    /*
     * Validate configuration.
     */
    static const char s_szMscWorkaround[] = "VRamSize"
                                            "|MonitorCount"
                                            "|FadeIn"
                                            "|FadeOut"
                                            "|LogoTime"
                                            "|LogoFile"
                                            "|ShowBootMenu"
                                            "|BiosRom"
                                            "|RealRetrace"
                                            "|CustomVideoModes"
                                            "|HeightReduction"
                                            "|CustomVideoMode1"
                                            "|CustomVideoMode2"
                                            "|CustomVideoMode3"
                                            "|CustomVideoMode4"
                                            "|CustomVideoMode5"
                                            "|CustomVideoMode6"
                                            "|CustomVideoMode7"
                                            "|CustomVideoMode8"
                                            "|CustomVideoMode9"
                                            "|CustomVideoMode10"
                                            "|CustomVideoMode11"
                                            "|CustomVideoMode12"
                                            "|CustomVideoMode13"
                                            "|CustomVideoMode14"
                                            "|CustomVideoMode15"
                                            "|CustomVideoMode16"
                                            "|MaxBiosXRes"
                                            "|MaxBiosYRes"
# ifdef VBOX_WITH_VMSVGA
                                            "|VMSVGAEnabled"
                                            "|VMSVGA10"
                                            "|VMSVGAPciId"
                                            "|VMSVGAPciBarLayout"
                                            "|VMSVGAFifoSize"
                                            "|VmSvga3"
                                            "|VmSvgaExposeLegacyVga"
# endif
# ifdef VBOX_WITH_VMSVGA3D
                                            "|VMSVGA3dEnabled"
                                            "|VMSVGA3dOverlayEnabled"
                                            "|VMSVGA3dMSAA"
                                            "|VMSVGA2dGBO"
# endif
                                            "|SuppressNewYearSplash"
                                            "|3DEnabled";

    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, s_szMscWorkaround, "");

    /*
     * Init state data.
     */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "VRamSize", &pThis->vram_size, VGA_VRAM_DEFAULT);
    AssertLogRelRCReturn(rc, rc);
    if (pThis->vram_size > VGA_VRAM_MAX)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   "VRamSize is too large, %#x, max %#x", pThis->vram_size, VGA_VRAM_MAX);
    if (pThis->vram_size < VGA_VRAM_MIN)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   "VRamSize is too small, %#x, max %#x", pThis->vram_size, VGA_VRAM_MIN);
    if (pThis->vram_size & (_256K - 1)) /* Make sure there are no partial banks even in planar modes. */
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   "VRamSize is not a multiple of 256K (%#x)", pThis->vram_size);

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "MonitorCount", &pThis->cMonitors, 1);
    AssertLogRelRCReturn(rc, rc);

    Log(("VGA: VRamSize=%#x fGCenabled=%RTbool fR0Enabled=%RTbool\n", pThis->vram_size, pDevIns->fRCEnabled, pDevIns->fR0Enabled));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "3DEnabled", &pThis->f3DEnabled, false);
    AssertLogRelRCReturn(rc, rc);
    Log(("VGA: f3DEnabled=%RTbool\n", pThis->f3DEnabled));

# ifdef VBOX_WITH_VMSVGA
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VMSVGAEnabled", &pThis->fVMSVGAEnabled, false);
    AssertLogRelRCReturn(rc, rc);
    Log(("VMSVGA: VMSVGAEnabled   = %d\n", pThis->fVMSVGAEnabled));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VMSVGA10", &pThis->fVMSVGA10, true);
    AssertLogRelRCReturn(rc, rc);
    Log(("VMSVGA: VMSVGA10        = %d\n", pThis->fVMSVGA10));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VMSVGAPciId", &pThis->fVMSVGAPciId, false);
    AssertLogRelRCReturn(rc, rc);
    Log(("VMSVGA: VMSVGAPciId   = %d\n", pThis->fVMSVGAPciId));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VMSVGAPciBarLayout", &pThis->fVMSVGAPciBarLayout, pThis->fVMSVGAPciId);
    AssertLogRelRCReturn(rc, rc);
    Log(("VMSVGA: VMSVGAPciBarLayout = %d\n", pThis->fVMSVGAPciBarLayout));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VmSvga3", &pThis->fVmSvga3, false);
    AssertLogRelRCReturn(rc, rc);
    Log(("VMSVGA: VmSvga3 = %RTbool\n", pThis->fVmSvga3));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VmSvgaExposeLegacyVga", &pThis->fLegacyVgaEnabled, true);
    AssertLogRelRCReturn(rc, rc);
    Log(("VMSVGA: VmSvgaExposeLegacyVga = %RTbool\n", pThis->fLegacyVgaEnabled));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "VMSVGAFifoSize", &pThis->svga.cbFIFO, VMSVGA_FIFO_SIZE);
    AssertLogRelRCReturn(rc, rc);
    AssertLogRelMsgReturn(pThis->svga.cbFIFO >= _128K, ("cbFIFO=%#x\n", pThis->svga.cbFIFO), VERR_OUT_OF_RANGE);
    AssertLogRelMsgReturn(pThis->svga.cbFIFO <=  _16M, ("cbFIFO=%#x\n", pThis->svga.cbFIFO), VERR_OUT_OF_RANGE);
    AssertLogRelMsgReturn(RT_IS_POWER_OF_TWO(pThis->svga.cbFIFO), ("cbFIFO=%#x\n", pThis->svga.cbFIFO), VERR_NOT_POWER_OF_TWO);
    pThis->svga.cbFIFOConfig = pThis->svga.cbFIFO;
    Log(("VMSVGA: VMSVGAFifoSize  = %#x (%'u)\n", pThis->svga.cbFIFO, pThis->svga.cbFIFO));
# endif
# ifdef VBOX_WITH_VMSVGA3D
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VMSVGA3dEnabled", &pThis->svga.f3DEnabled, false);
    AssertLogRelRCReturn(rc, rc);
    Log(("VMSVGA: VMSVGA3dEnabled = %d\n", pThis->svga.f3DEnabled));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VMSVGA3dOverlayEnabled", &pThis->svga.f3DOverlayEnabled, false);
    AssertLogRelRCReturn(rc, rc);
    Log(("VMSVGA: VMSVGA3dOverlayEnabled = %d\n", pThis->svga.f3DOverlayEnabled));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VMSVGA3dMSAA", &pThis->svga.fVMSVGA3dMSAA, true);
    AssertLogRelRCReturn(rc, rc);
    Log(("VMSVGA: VMSVGA3dMSAA = %d\n", pThis->svga.fVMSVGA3dMSAA));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "VMSVGA2dGBO", &pThis->svga.fVMSVGA2dGBO, true);
    AssertLogRelRCReturn(rc, rc);
    if (pThis->svga.f3DEnabled || pThis->fVMSVGAPciId == 0) /* The fVMSVGA2dGBO is for 2D mode of vmwgfx.ko only*/
        pThis->svga.fVMSVGA2dGBO = false;
    Log(("VMSVGA: fVMSVGA2dGBO = %d\n", pThis->svga.fVMSVGA2dGBO));
# endif

# ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAPciBarLayout)
    {
        if (pThis->fVmSvga3)
        {
            pThis->pciRegions.iIO   = 0;
            pThis->pciRegions.iVRAM = 2;
        }
        else
        {
            pThis->pciRegions.iIO   = 0;
            pThis->pciRegions.iVRAM = 1;
        }
    }
    else
    {
        pThis->pciRegions.iVRAM = 0;
        pThis->pciRegions.iIO   = 1;
    }
    pThis->pciRegions.iFIFO = 2;
# else
    pThis->pciRegions.iVRAM = 0;
# endif

    pThisCC->pDevIns = pDevIns;

    vgaR3Reset(pDevIns);

    /* The PCI devices configuration. */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

# ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAEnabled)
    {
        /* Extend our VGA device with VMWare SVGA functionality. */
        if (pThis->fVMSVGAPciId)
        {
            PDMPciDevSetVendorId(pPciDev,       PCI_VENDOR_ID_VMWARE);
            if (pThis->fVmSvga3)
                PDMPciDevSetDeviceId(pPciDev,   PCI_DEVICE_ID_VMWARE_SVGA3);
            else
                PDMPciDevSetDeviceId(pPciDev,   PCI_DEVICE_ID_VMWARE_SVGA2);
        }
        else
        {
            PDMPciDevSetVendorId(pPciDev,       0x80ee);   /* PCI vendor, just a free bogus value */
            PDMPciDevSetDeviceId(pPciDev,       0xbeef);
        }
        PDMPciDevSetSubSystemVendorId(pPciDev,  PCI_VENDOR_ID_VMWARE);
        if (pThis->fVmSvga3)
            PDMPciDevSetSubSystemId(pPciDev,    PCI_DEVICE_ID_VMWARE_SVGA3);
        else
            PDMPciDevSetSubSystemId(pPciDev,    PCI_DEVICE_ID_VMWARE_SVGA2);
    }
    else
# endif /* VBOX_WITH_VMSVGA */
    {
        PDMPciDevSetVendorId(pPciDev,           0x80ee);   /* PCI vendor, just a free bogus value */
        PDMPciDevSetDeviceId(pPciDev,           0xbeef);
    }
    PDMPciDevSetClassSub(pPciDev,               0x00);   /* VGA controller */
    PDMPciDevSetClassBase(pPciDev,              0x03);
    PDMPciDevSetHeaderType(pPciDev,             0x00);
# if defined(VBOX_WITH_HGSMI) && (defined(VBOX_WITH_VDMA) || defined(VBOX_WITH_WDDM))
    PDMPciDevSetInterruptPin(pPciDev,           1);
# endif

    /* the interfaces. */
    pThisCC->IBase.pfnQueryInterface    = vgaR3PortQueryInterface;

    pThisCC->IPort.pfnUpdateDisplay     = vgaR3PortUpdateDisplay;
    pThisCC->IPort.pfnUpdateDisplayAll  = vgaR3PortUpdateDisplayAll;
    pThisCC->IPort.pfnQueryVideoMode    = vgaR3PortQueryVideoMode;
    pThisCC->IPort.pfnSetRefreshRate    = vgaR3PortSetRefreshRate;
    pThisCC->IPort.pfnTakeScreenshot    = vgaR3PortTakeScreenshot;
    pThisCC->IPort.pfnFreeScreenshot    = vgaR3PortFreeScreenshot;
    pThisCC->IPort.pfnDisplayBlt        = vgaR3PortDisplayBlt;
    pThisCC->IPort.pfnUpdateDisplayRect = vgaR3PortUpdateDisplayRect;
    pThisCC->IPort.pfnCopyRect          = vgaR3PortCopyRect;
    pThisCC->IPort.pfnSetRenderVRAM     = vgaR3PortSetRenderVRAM;
    pThisCC->IPort.pfnSetViewport       = NULL;
    pThisCC->IPort.pfnReportMonitorPositions = NULL;
# ifdef VBOX_WITH_VMSVGA
    if (pThis->fVMSVGAEnabled)
    {
        pThisCC->IPort.pfnSetViewport = vmsvgaR3PortSetViewport;
        pThisCC->IPort.pfnReportMonitorPositions = vmsvgaR3PortReportMonitorPositions;
    }
# endif
    pThisCC->IPort.pfnSendModeHint      = vbvaR3PortSendModeHint;
    pThisCC->IPort.pfnReportHostCursorCapabilities = vgaR3PortReportHostCursorCapabilities;
    pThisCC->IPort.pfnReportHostCursorPosition = vgaR3PortReportHostCursorPosition;

    pThisCC->ILeds.pfnQueryStatusLed    = vgaR3PortQueryStatusLed;
    pThis->Led3D.u32Magic               = PDMLED_MAGIC;

    /*
     * We use our own critical section to avoid unncessary pointer indirections
     * in interface methods (as well as for historical reasons).
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "VGA#%u", iInstance);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, &pThis->CritSect);
    AssertRCReturn(rc, rc);

# ifdef VBOX_WITH_HGSMI
    /*
     * This critical section is used by vgaR3IOPortHgsmiWrite, VBVARaiseIrq and VBVAOnResume
     * for some IRQ related synchronization.
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSectIRQ, RT_SRC_POS, "VGA#%u_IRQ", iInstance);
    AssertRCReturn(rc, rc);
# endif

    /*
     * PCI device registration.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
        return rc;
    /*AssertMsg(pThis->Dev.uDevFn == 16 || iInstance != 0, ("pThis->Dev.uDevFn=%d\n", pThis->Dev.uDevFn));*/
    if (pPciDev->uDevFn != 16 && iInstance == 0)
        Log(("!!WARNING!!: pThis->dev.uDevFn=%d (ignore if testcase or not started by Main)\n", pPciDev->uDevFn));

# ifdef VBOX_WITH_VMSVGA
    pThis->hIoPortVmSvga    = NIL_IOMIOPORTHANDLE;
    pThis->hMmio2VmSvgaFifo = NIL_PGMMMIO2HANDLE;
    pThis->hMmioSvga3       = NIL_IOMMMIOHANDLE;
    if (pThis->fVMSVGAEnabled)
    {
        if (pThis->fVmSvga3)
        {
            /* Register the MMIO register region. */
            rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, pThis->pciRegions.iIO, 4096 * 1024, PCI_ADDRESS_SPACE_MEM,
                                                vmsvga3MmioWrite, vmsvga3MmioRead, NULL /*pvUser*/,
                                                IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED,
                                                "VMSVGA3-MMIO", &pThis->hMmioSvga3);
            AssertRCReturn(rc, rc);
        }
        else
        {
            /* Register the io command ports. */
            rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, pThis->pciRegions.iIO, 0x10, vmsvgaIOWrite, vmsvgaIORead, NULL /*pvUser*/,
                                              "VMSVGA", NULL /*paExtDescs*/, &pThis->hIoPortVmSvga);
            AssertRCReturn(rc, rc);

            rc = PDMDevHlpPCIIORegionCreateMmio2Ex(pDevIns, pThis->pciRegions.iFIFO, pThis->svga.cbFIFO,
                                                   PCI_ADDRESS_SPACE_MEM, 0 /*fFlags*/, vmsvgaR3PciIORegionFifoMapUnmap,
                                                   "VMSVGA-FIFO", (void **)&pThisCC->svga.pau32FIFO, &pThis->hMmio2VmSvgaFifo);
            AssertRCReturn(rc, PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                                   N_("Failed to create VMSVGA FIFO (%u bytes)"), pThis->svga.cbFIFO));
        }

        pPciDev->pfnRegionLoadChangeHookR3 = vgaR3PciRegionLoadChangeHook;
    }
# endif /* VBOX_WITH_VMSVGA */

    /*
     * Allocate VRAM and create a PCI region for it.
     */
    rc = PDMDevHlpPCIIORegionCreateMmio2Ex(pDevIns, pThis->pciRegions.iVRAM, pThis->vram_size,
                                           PCI_ADDRESS_SPACE_MEM_PREFETCH, PGMPHYS_MMIO2_FLAGS_TRACK_DIRTY_PAGES,
                                           vgaR3PciIORegionVRamMapUnmap, "VRam", (void **)&pThisCC->pbVRam, &pThis->hMmio2VRam);
    AssertLogRelRCReturn(rc, PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                                 N_("Failed to allocate %u bytes of VRAM"), pThis->vram_size));

    if (pThis->fLegacyVgaEnabled)
    {
        /*
         * Register I/O ports.
         */
# define REG_PORT(a_uPort, a_cPorts, a_pfnWrite, a_pfnRead, a_szDesc, a_phIoPort) do { \
            rc = PDMDevHlpIoPortCreateFlagsAndMap(pDevIns, a_uPort, a_cPorts, IOM_IOPORT_F_ABS, \
                                                  a_pfnWrite, a_pfnRead, "VGA - " a_szDesc, NULL /*paExtDescs*/, a_phIoPort); \
            AssertRCReturn(rc, rc); \
        } while (0)
        REG_PORT(0x3c0,  2, vgaIoPortArWrite,       vgaIoPortArRead,        "Attribute Controller",     &pThis->hIoPortAr);
        REG_PORT(0x3c2,  1, vgaIoPortMsrWrite,      vgaIoPortSt00Read,      "MSR / ST00",               &pThis->hIoPortMsrSt00);
        REG_PORT(0x3c3,  1, vgaIoPortUnusedWrite,   vgaIoPortUnusedRead,    "0x3c3",                    &pThis->hIoPort3c3);
        REG_PORT(0x3c4,  2, vgaIoPortSrWrite,       vgaIoPortSrRead,        "Sequencer",                &pThis->hIoPortSr);
        REG_PORT(0x3c6,  4, vgaIoPortDacWrite,      vgaIoPortDacRead,       "DAC",                      &pThis->hIoPortDac);
        REG_PORT(0x3ca,  4, vgaIoPortPosWrite,      vgaIoPortPosRead,       "Graphics Position", /*?*/  &pThis->hIoPortPos);
        REG_PORT(0x3ce,  2, vgaIoPortGrWrite,       vgaIoPortGrRead,        "Graphics Controller",      &pThis->hIoPortGr);

        /* Note! Ralf Brown lists 0x3b0-0x3b1, 0x3b2-0x3b3 and 0x3b6-0x3b7 as "the same as" 0x3b4-0x3b5. */
        REG_PORT(0x3b4,  2, vgaIoPortMdaCrtWrite,   vgaIoPortMdaCrtRead,    "MDA CRT control",          &pThis->hIoPortMdaCrt);
        REG_PORT(0x3ba,  1, vgaIoPortMdaFcrWrite,   vgaIoPortMdaStRead,     "MDA feature/status",       &pThis->hIoPortMdaFcrSt);
        REG_PORT(0x3d4,  2, vgaIoPortCgaCrtWrite,   vgaIoPortCgaCrtRead,    "CGA CRT control",          &pThis->hIoPortCgaCrt);
        REG_PORT(0x3da,  1, vgaIoPortCgaFcrWrite,   vgaIoPortCgaStRead,     "CGA Feature / status",     &pThis->hIoPortCgaFcrSt);

# ifdef CONFIG_BOCHS_VBE
        REG_PORT(0x1ce,  1, vgaIoPortWriteVbeIndex, vgaIoPortReadVbeIndex,  "VBE Index",                &pThis->hIoPortVbeIndex);
        REG_PORT(0x1cf,  1, vgaIoPortWriteVbeData,  vgaIoPortReadVbeData,   "VBE Data",                 &pThis->hIoPortVbeData);
# endif /* CONFIG_BOCHS_VBE */

# ifdef VBOX_WITH_HGSMI
        /* Use reserved VGA IO ports for HGSMI. */
        REG_PORT(VGA_PORT_HGSMI_HOST,  4, vgaR3IOPortHgsmiWrite, vgaR3IOPortHgmsiRead, "HGSMI host (3b0-3b3)",  &pThis->hIoPortHgsmiHost);
        REG_PORT(VGA_PORT_HGSMI_GUEST, 4, vgaR3IOPortHgsmiWrite, vgaR3IOPortHgmsiRead, "HGSMI guest (3d0-3d3)", &pThis->hIoPortHgsmiGuest);
# endif /* VBOX_WITH_HGSMI */

# undef REG_PORT

        /* vga bios */
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, VBE_PRINTF_PORT, 1 /*cPorts*/, vgaIoPortWriteBios, vgaIoPortReadBios,
                                         "VGA BIOS debug/panic", NULL /*paExtDescs*/, &pThis->hIoPortBios);
        AssertRCReturn(rc, rc);

        /*
         * The MDA/CGA/EGA/VGA/whatever fixed MMIO area.
         */
        rc = PDMDevHlpMmioCreateExAndMap(pDevIns, 0x000a0000, 0x00020000,
                                         IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU | IOMMMIO_FLAGS_ABS,
                                         NULL /*pPciDev*/, UINT32_MAX /*iPciRegion*/,
                                         vgaMmioWrite, vgaMmioRead, vgaMmioFill, NULL /*pvUser*/,
                                         "VGA - VGA Video Buffer", &pThis->hMmioLegacy);
        AssertRCReturn(rc, rc);

        /*
         * Get the VGA BIOS ROM file name.
         */
        rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "BiosRom", &pThisCC->pszVgaBiosFile);
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        {
            pThisCC->pszVgaBiosFile = NULL;
            rc = VINF_SUCCESS;
        }
        else if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"BiosRom\" as a string failed"));
        else if (!*pThisCC->pszVgaBiosFile)
        {
            PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszVgaBiosFile);
            pThisCC->pszVgaBiosFile = NULL;
        }

        /*
         * Determine the VGA BIOS ROM size, open specified ROM file in the process.
         */
        RTFILE FileVgaBios = NIL_RTFILE;
        if (pThisCC->pszVgaBiosFile)
        {
            rc = RTFileOpen(&FileVgaBios, pThisCC->pszVgaBiosFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
            if (RT_SUCCESS(rc))
            {
                rc = RTFileQuerySize(FileVgaBios, &pThisCC->cbVgaBios);
                if (RT_SUCCESS(rc))
                {
                    if (    RT_ALIGN(pThisCC->cbVgaBios, _4K) != pThisCC->cbVgaBios
                        ||  pThisCC->cbVgaBios > _64K
                        ||  pThisCC->cbVgaBios < 16 * _1K)
                        rc = VERR_TOO_MUCH_DATA;
                }
            }
            if (RT_FAILURE(rc))
            {
                /*
                 * In case of failure simply fall back to the built-in VGA BIOS ROM.
                 */
                Log(("vgaConstruct: Failed to open VGA BIOS ROM file '%s', rc=%Rrc!\n", pThisCC->pszVgaBiosFile, rc));
                RTFileClose(FileVgaBios);
                FileVgaBios = NIL_RTFILE;
                PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszVgaBiosFile);
                pThisCC->pszVgaBiosFile = NULL;
            }
        }

        /*
         * Attempt to get the VGA BIOS ROM data from file.
         */
        if (pThisCC->pszVgaBiosFile)
        {
            /*
             * Allocate buffer for the VGA BIOS ROM data.
             */
            pThisCC->pbVgaBios = (uint8_t *)PDMDevHlpMMHeapAlloc(pDevIns, pThisCC->cbVgaBios);
            if (pThisCC->pbVgaBios)
            {
                rc = RTFileRead(FileVgaBios, pThisCC->pbVgaBios, pThisCC->cbVgaBios, NULL);
                if (RT_FAILURE(rc))
                {
                    AssertMsgFailed(("RTFileRead(,,%d,NULL) -> %Rrc\n", pThisCC->cbVgaBios, rc));
                    PDMDevHlpMMHeapFree(pDevIns, pThisCC->pbVgaBios);
                    pThisCC->pbVgaBios = NULL;
                }
                rc = VINF_SUCCESS;
            }
            /* else: Out of memory condition is ignored, see below. */
        }
        else
            pThisCC->pbVgaBios = NULL;

        /* cleanup */
        if (FileVgaBios != NIL_RTFILE)
            RTFileClose(FileVgaBios);

        /* If we were unable to get the data from file for whatever reason, fall
           back to the built-in ROM image. */
        const uint8_t  *pbVgaBiosBinary;
        uint64_t        cbVgaBiosBinary;
        uint32_t        fFlags = 0;
        if (pThisCC->pbVgaBios == NULL)
        {
            CPUMMICROARCH enmMicroarch = PDMDevHlpCpuGetGuestMicroarch(pDevIns);
            if (   enmMicroarch == kCpumMicroarch_Intel_8086
                || enmMicroarch == kCpumMicroarch_Intel_80186
                || enmMicroarch == kCpumMicroarch_NEC_V20
                || enmMicroarch == kCpumMicroarch_NEC_V30)
            {
                pbVgaBiosBinary = g_abVgaBiosBinary8086;
                cbVgaBiosBinary = g_cbVgaBiosBinary8086;
                LogRel(("VGA: Using the 8086 BIOS image!\n"));
            }
            else if (enmMicroarch == kCpumMicroarch_Intel_80286)
            {
                pbVgaBiosBinary = g_abVgaBiosBinary286;
                cbVgaBiosBinary = g_cbVgaBiosBinary286;
                LogRel(("VGA: Using the 286 BIOS image!\n"));
            }
            else
            {
                pbVgaBiosBinary = g_abVgaBiosBinary386;
                cbVgaBiosBinary = g_cbVgaBiosBinary386;
                LogRel(("VGA: Using the 386+ BIOS image.\n"));
            }
            fFlags          = PGMPHYS_ROM_FLAGS_PERMANENT_BINARY;
        }
        else
        {
            pbVgaBiosBinary = pThisCC->pbVgaBios;
            cbVgaBiosBinary = pThisCC->cbVgaBios;
        }

        AssertReleaseMsg(cbVgaBiosBinary <= _64K && cbVgaBiosBinary >= 32*_1K, ("cbVgaBiosBinary=%#x\n", cbVgaBiosBinary));
        AssertReleaseMsg(RT_ALIGN_Z(cbVgaBiosBinary, GUEST_PAGE_SIZE) == cbVgaBiosBinary, ("cbVgaBiosBinary=%#x\n", cbVgaBiosBinary));
        /* Note! Because of old saved states we'll always register at least 36KB of ROM. */
        rc = PDMDevHlpROMRegister(pDevIns, 0x000c0000, RT_MAX(cbVgaBiosBinary, 36*_1K), pbVgaBiosBinary, cbVgaBiosBinary,
                                  fFlags, "VGA BIOS");
        AssertRCReturn(rc, rc);
    }

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, VGA_SAVEDSTATE_VERSION, sizeof(*pThis), NULL,
                                NULL,          vgaR3LiveExec, NULL,
                                vgaR3SavePrep, vgaR3SaveExec, vgaR3SaveDone,
                                vgaR3LoadPrep, vgaR3LoadExec, vgaR3LoadDone);
    AssertRCReturn(rc, rc);

    /*
     * Create the refresh timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_REAL, vgaR3TimerRefresh, NULL,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_NO_RING0, "VGA Refresh", &pThis->hRefreshTimer);
    AssertRCReturn(rc, rc);

    /*
     * Attach to the display.
     */
    rc = vgaAttach(pDevIns, 0 /* display LUN # */, PDM_TACH_FLAGS_NOT_HOT_PLUG);
    AssertRCReturn(rc, rc);

    /*
     * Initialize the retrace flag.
     */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "RealRetrace", &pThis->fRealRetrace, false);
    AssertLogRelRCReturn(rc, rc);

    uint16_t maxBiosXRes;
    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "MaxBiosXRes", &maxBiosXRes, UINT16_MAX);
    AssertLogRelRCReturn(rc, rc);
    uint16_t maxBiosYRes;
    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "MaxBiosYRes", &maxBiosYRes, UINT16_MAX);
    AssertLogRelRCReturn(rc, rc);

    if (pThis->fLegacyVgaEnabled)
    {
        /*
         * Compute buffer size for the VBE BIOS Extra Data.
         */
        cb = sizeof(mode_info_list) + sizeof(ModeInfoListItem);

        rc = pHlp->pfnCFGMQueryU32(pCfg, "HeightReduction", &cyReduction);
        if (RT_SUCCESS(rc) && cyReduction)
            cb *= 2;                            /* Default mode list will be twice long */
        else
            cyReduction = 0;

        rc = pHlp->pfnCFGMQueryU32(pCfg, "CustomVideoModes", &cCustomModes);
        if (RT_SUCCESS(rc) && cCustomModes)
            cb += sizeof(ModeInfoListItem) * cCustomModes;
        else
            cCustomModes = 0;

        /*
         * Allocate and initialize buffer for the VBE BIOS Extra Data.
         */
        AssertRelease(sizeof(VBEHEADER) + cb < 65536);
        pThisCC->cbVBEExtraData = (uint16_t)(sizeof(VBEHEADER) + cb);
        pThisCC->pbVBEExtraData = (uint8_t *)PDMDevHlpMMHeapAllocZ(pDevIns, pThisCC->cbVBEExtraData);
        if (!pThisCC->pbVBEExtraData)
            return VERR_NO_MEMORY;

        pVBEDataHdr = (PVBEHEADER)pThisCC->pbVBEExtraData;
        pVBEDataHdr->u16Signature = VBEHEADER_MAGIC;
        pVBEDataHdr->cbData = cb;

        pCurMode = (ModeInfoListItem *)(pVBEDataHdr + 1);
        for (i = 0; i < MODE_INFO_SIZE; i++)
        {
            uint32_t pixelWidth, reqSize;
            if (mode_info_list[i].info.MemoryModel == VBE_MEMORYMODEL_TEXT_MODE)
                pixelWidth = 2;
            else
                pixelWidth = (mode_info_list[i].info.BitsPerPixel +7) / 8;
            reqSize = mode_info_list[i].info.XResolution
                    * mode_info_list[i].info.YResolution
                    * pixelWidth;
            if (reqSize >= pThis->vram_size)
                continue;
            if (!reqSize)
                continue;
            if (   mode_info_list[i].info.XResolution > maxBiosXRes
                || mode_info_list[i].info.YResolution > maxBiosYRes)
                continue;
            *pCurMode = mode_info_list[i];
            vgaR3AdjustModeInfo(pThis, pCurMode);
            pCurMode++;
        }

        /*
         * Copy default modes with subtracted YResolution.
         */
        if (cyReduction)
        {
            ModeInfoListItem *pDefMode = mode_info_list;
            Log(("vgaR3Construct: cyReduction=%u\n", cyReduction));
            for (i = 0; i < MODE_INFO_SIZE; i++, pDefMode++)
            {
                uint32_t pixelWidth, reqSize;
                if (pDefMode->info.MemoryModel == VBE_MEMORYMODEL_TEXT_MODE)
                    pixelWidth = 2;
                else
                    pixelWidth = (pDefMode->info.BitsPerPixel + 7) / 8;
                reqSize = pDefMode->info.XResolution * pDefMode->info.YResolution *  pixelWidth;
                if (reqSize >= pThis->vram_size)
                    continue;
                if (   pDefMode->info.XResolution > maxBiosXRes
                    || pDefMode->info.YResolution - cyReduction > maxBiosYRes)
                    continue;
                *pCurMode = *pDefMode;
                pCurMode->mode += 0x30;
                pCurMode->info.YResolution -= cyReduction;
                pCurMode++;
            }
        }


        /*
         * Add custom modes.
         */
        if (cCustomModes)
        {
            uint16_t u16CurMode = VBE_VBOX_MODE_CUSTOM1;
            for (i = 1; i <= cCustomModes; i++)
            {
                char szExtraDataKey[sizeof("CustomVideoModeXX")];
                char *pszExtraData = NULL;

                /* query and decode the custom mode string. */
                RTStrPrintf(szExtraDataKey, sizeof(szExtraDataKey), "CustomVideoMode%d", i);
                rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, szExtraDataKey, &pszExtraData);
                if (RT_SUCCESS(rc))
                {
                    ModeInfoListItem *pDefMode = mode_info_list;
                    unsigned int cx, cy, cBits, cParams, j;
                    uint16_t u16DefMode;

                    cParams = sscanf(pszExtraData, "%ux%ux%u", &cx, &cy, &cBits);
                    if (    cParams != 3
                        ||  (cBits != 8 && cBits != 16 && cBits != 24 && cBits != 32))
                    {
                        AssertMsgFailed(("Configuration error: Invalid mode data '%s' for '%s'! cBits=%d\n", pszExtraData, szExtraDataKey, cBits));
                        return VERR_VGA_INVALID_CUSTOM_MODE;
                    }
                    if (!cx || !cy)
                    {
                        AssertMsgFailed(("Configuration error: Invalid mode data '%s' for '%s'! cx=%u, cy=%u\n", pszExtraData, szExtraDataKey, cx, cy));
                        return VERR_VGA_INVALID_CUSTOM_MODE;
                    }
                    cbPitch = calc_line_pitch(cBits, cx);
                    if (cy * cbPitch >= pThis->vram_size)
                    {
                        AssertMsgFailed(("Configuration error: custom video mode %dx%dx%dbits is too large for the virtual video memory of %dMb.  Please increase the video memory size.\n",
                                         cx, cy, cBits, pThis->vram_size / _1M));
                        return VERR_VGA_INVALID_CUSTOM_MODE;
                    }
                    PDMDevHlpMMHeapFree(pDevIns, pszExtraData);

                    /* Use defaults from max@bpp mode. */
                    switch (cBits)
                    {
                        case 8:
                            u16DefMode = VBE_VESA_MODE_1024X768X8;
                            break;

                        case 16:
                            u16DefMode = VBE_VESA_MODE_1024X768X565;
                            break;

                        case 24:
                            u16DefMode = VBE_VESA_MODE_1024X768X888;
                            break;

                        case 32:
                            u16DefMode = VBE_OWN_MODE_1024X768X8888;
                            break;

                        default: /* gcc, shut up! */
                            AssertMsgFailed(("gone postal!\n"));
                            continue;
                    }

                    /* mode_info_list is not terminated */
                    for (j = 0; j < MODE_INFO_SIZE && pDefMode->mode != u16DefMode; j++)
                        pDefMode++;
                    Assert(j < MODE_INFO_SIZE);

                    *pCurMode  = *pDefMode;
                    pCurMode->mode = u16CurMode++;

                    /* adjust defaults */
                    pCurMode->info.XResolution = cx;
                    pCurMode->info.YResolution = cy;
                    pCurMode->info.BytesPerScanLine    = cbPitch;
                    pCurMode->info.LinBytesPerScanLine = cbPitch;
                    vgaR3AdjustModeInfo(pThis, pCurMode);

                    /* commit it */
                    pCurMode++;
                }
                else if (rc != VERR_CFGM_VALUE_NOT_FOUND)
                {
                    AssertMsgFailed(("pHlp->pfnCFGMQueryStringAlloc(,'%s',) -> %Rrc\n", szExtraDataKey, rc));
                    return rc;
                }
            } /* foreach custom mode key */
        }

        /*
         * Add the "End of list" mode.
         */
        memset(pCurMode, 0, sizeof(*pCurMode));
        pCurMode->mode = VBE_VESA_MODE_END_OF_LIST;

        /*
         * Register I/O Port for the VBE BIOS Extra Data.
         */
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, VBE_EXTRA_PORT, 1 /*cPorts*/, vbeR3IOPortWriteVbeExtra, vbeR3IoPortReadVbeExtra,
                                         "VBE BIOS Extra Data", NULL /*paExtDesc*/, &pThis->hIoPortVbeExtra);
        AssertRCReturn(rc, rc);

        /*
         * Register I/O Port for the BIOS Logo.
         */
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, LOGO_IO_PORT, 1 /*cPorts*/, vbeR3IoPortWriteCmdLogo, vbeR3IoPortReadCmdLogo,
                                         "BIOS Logo", NULL /*paExtDesc*/, &pThis->hIoPortCmdLogo);
        AssertRCReturn(rc, rc);

        /*
         * Register debugger info callbacks.
         */
        PDMDevHlpDBGFInfoRegister(pDevIns, "vga", "Display basic VGA state.", vgaR3InfoState);
        PDMDevHlpDBGFInfoRegister(pDevIns, "vgatext", "Display VGA memory formatted as text.", vgaR3InfoText);
        PDMDevHlpDBGFInfoRegister(pDevIns, "vgacr", "Dump VGA CRTC registers.", vgaR3InfoCR);
        PDMDevHlpDBGFInfoRegister(pDevIns, "vgagr", "Dump VGA Graphics Controller registers.", vgaR3InfoGR);
        PDMDevHlpDBGFInfoRegister(pDevIns, "vgasr", "Dump VGA Sequencer registers.", vgaR3InfoSR);
        PDMDevHlpDBGFInfoRegister(pDevIns, "vgaar", "Dump VGA Attribute Controller registers.", vgaR3InfoAR);
        PDMDevHlpDBGFInfoRegister(pDevIns, "vgapl", "Dump planar graphics state.", vgaR3InfoPlanar);
        PDMDevHlpDBGFInfoRegister(pDevIns, "vgadac", "Dump VGA DAC registers.", vgaR3InfoDAC);
        PDMDevHlpDBGFInfoRegister(pDevIns, "vbe", "Dump VGA VBE registers.", vgaR3InfoVBE);
    }

    /*
     * Construct the logo header.
     */
    LOGOHDR LogoHdr = { LOGO_HDR_MAGIC, 0, 0, 0, 0, 0, 0 };

    rc = pHlp->pfnCFGMQueryU8(pCfg, "FadeIn", &LogoHdr.fu8FadeIn);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        LogoHdr.fu8FadeIn = 1;
    else if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"FadeIn\" as integer failed"));

    rc = pHlp->pfnCFGMQueryU8(pCfg, "FadeOut", &LogoHdr.fu8FadeOut);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        LogoHdr.fu8FadeOut = 1;
    else if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"FadeOut\" as integer failed"));

    rc = pHlp->pfnCFGMQueryU16(pCfg, "LogoTime", &LogoHdr.u16LogoMillies);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        LogoHdr.u16LogoMillies = 0;
    else if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"LogoTime\" as integer failed"));

    rc = pHlp->pfnCFGMQueryU8(pCfg, "ShowBootMenu", &LogoHdr.fu8ShowBootMenu);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        LogoHdr.fu8ShowBootMenu = 0;
    else if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"ShowBootMenu\" as integer failed"));

# if defined(DEBUG) && !defined(DEBUG_sunlover) && !defined(DEBUG_michael)
    /* Disable the logo abd menu if all default settings. */
    if (   LogoHdr.fu8FadeIn
        && LogoHdr.fu8FadeOut
        && LogoHdr.u16LogoMillies == 0
        && LogoHdr.fu8ShowBootMenu == 2)
    {
        LogoHdr.fu8FadeIn = LogoHdr.fu8FadeOut = 0;
        LogoHdr.u16LogoMillies = 500;
    }
# endif

    /* Delay the logo a little bit */
    if (LogoHdr.fu8FadeIn && LogoHdr.fu8FadeOut && !LogoHdr.u16LogoMillies)
        LogoHdr.u16LogoMillies = RT_MAX(LogoHdr.u16LogoMillies, LOGO_DELAY_TIME);

    /*
     * Get the Logo file name.
     */
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "LogoFile", &pThisCC->pszLogoFile);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pThisCC->pszLogoFile = NULL;
    else if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"LogoFile\" as a string failed"));
    else if (!*pThisCC->pszLogoFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszLogoFile);
        pThisCC->pszLogoFile = NULL;
    }

    /*
     * Determine the logo size, open any specified logo file in the process.
     */
    LogoHdr.cbLogo = g_cbVgaDefBiosLogo;
    RTFILE FileLogo = NIL_RTFILE;
    if (pThisCC->pszLogoFile)
    {
        rc = RTFileOpen(&FileLogo, pThisCC->pszLogoFile,
                        RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        if (RT_SUCCESS(rc))
        {
            uint64_t cbFile;
            rc = RTFileQuerySize(FileLogo, &cbFile);
            if (RT_SUCCESS(rc))
            {
                if (cbFile > 0 && cbFile < 32*_1M)
                    LogoHdr.cbLogo = (uint32_t)cbFile;
                else
                    rc = VERR_TOO_MUCH_DATA;
            }
        }
        if (RT_FAILURE(rc))
        {
            /*
             * Ignore failure and fall back to the default logo.
             */
            LogRel(("vgaR3Construct: Failed to open logo file '%s', rc=%Rrc!\n", pThisCC->pszLogoFile, rc));
            if (FileLogo != NIL_RTFILE)
                RTFileClose(FileLogo);
            FileLogo = NIL_RTFILE;
            PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszLogoFile);
            pThisCC->pszLogoFile = NULL;
        }
    }

    /*
     * Disable graphic splash screen if it doesn't fit into VRAM.
     */
    if (pThis->vram_size < LOGO_MAX_SIZE)
        LogoHdr.fu8FadeIn = LogoHdr.fu8FadeOut = LogoHdr.u16LogoMillies = 0;

    /*
     * Allocate buffer for the logo data.
     * Let us fall back to default logo on read failure.
     */
    pThisCC->cbLogo = LogoHdr.cbLogo;
    if (g_cbVgaDefBiosLogo)
        pThisCC->cbLogo = RT_MAX(pThisCC->cbLogo, g_cbVgaDefBiosLogo);
    pThisCC->cbLogo += sizeof(LogoHdr);

    pThisCC->pbLogo = (uint8_t *)PDMDevHlpMMHeapAlloc(pDevIns, pThisCC->cbLogo);
    if (pThisCC->pbLogo)
    {
        /*
         * Write the logo header.
         */
        PLOGOHDR pLogoHdr = (PLOGOHDR)pThisCC->pbLogo;
        *pLogoHdr = LogoHdr;

        /*
         * Write the logo bitmap.
         */
        if (pThisCC->pszLogoFile)
        {
            rc = RTFileRead(FileLogo, pLogoHdr + 1, LogoHdr.cbLogo, NULL);
            if (RT_SUCCESS(rc))
                rc = vbeR3ParseBitmap(pThisCC);
            if (RT_FAILURE(rc))
            {
                LogRel(("Error %Rrc reading logo file '%s', using internal logo\n",
                       rc, pThisCC->pszLogoFile));
                pLogoHdr->cbLogo = LogoHdr.cbLogo = g_cbVgaDefBiosLogo;
            }
        }
        if (   !pThisCC->pszLogoFile
            || RT_FAILURE(rc))
        {
            memcpy(pLogoHdr + 1, g_abVgaDefBiosLogo, LogoHdr.cbLogo);
            rc = vbeR3ParseBitmap(pThisCC);
            AssertLogRelMsgReturn(RT_SUCCESS(rc), ("Parsing of internal bitmap failed! vbeR3ParseBitmap() -> %Rrc\n", rc), rc);
        }

        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NO_MEMORY;

    /*
     * Cleanup.
     */
    if (FileLogo != NIL_RTFILE)
        RTFileClose(FileLogo);

# ifdef VBOX_WITH_HGSMI
    VBVAInit(pDevIns, pThis, pThisCC);
# endif

# ifdef VBOX_WITH_VDMA
    if (rc == VINF_SUCCESS)
    {
        rc = vboxVDMAConstruct(pThis, pThisCC, 1024);
        AssertRC(rc);
    }
# endif

# ifdef VBOX_WITH_VMSVGA
    if (    rc == VINF_SUCCESS
        &&  pThis->fVMSVGAEnabled)
        rc = vmsvgaR3Init(pDevIns);
# endif

    /*
     * Statistics.
     */
# ifdef VBOX_WITH_STATISTICS
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRZMemoryRead,  STAMTYPE_PROFILE, "RZ/MMIO-Read",  STAMUNIT_TICKS_PER_CALL, "Profiling of the VGAGCMemoryRead() body.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatR3MemoryRead,  STAMTYPE_PROFILE, "R3/MMIO-Read",  STAMUNIT_TICKS_PER_CALL, "Profiling of the VGAGCMemoryRead() body.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRZMemoryWrite, STAMTYPE_PROFILE, "RZ/MMIO-Write", STAMUNIT_TICKS_PER_CALL, "Profiling of the VGAGCMemoryWrite() body.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatR3MemoryWrite, STAMTYPE_PROFILE, "R3/MMIO-Write", STAMUNIT_TICKS_PER_CALL, "Profiling of the VGAGCMemoryWrite() body.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMapPage,       STAMTYPE_COUNTER, "MapPageCalls",  STAMUNIT_OCCURENCES,     "Calls to IOMMmioMapMmio2Page.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMapReset,      STAMTYPE_COUNTER, "MapPageReset",  STAMUNIT_OCCURENCES,     "Calls to IOMMmioResetRegion.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatUpdateDisp,    STAMTYPE_COUNTER, "UpdateDisplay", STAMUNIT_OCCURENCES,     "Calls to vgaR3PortUpdateDisplay().");
# endif
# ifdef VBOX_WITH_HGSMI
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatHgsmiMdaCgaAccesses, STAMTYPE_COUNTER, "HgmsiMdaCgaAccesses", STAMUNIT_OCCURENCES, "Number of non-HGMSI accesses for 03b0-3b3 and 03d0-3d3.");
# endif

    /* Init latched access mask. */
    pThis->uMaskLatchAccess = 0x3ff;

    if (RT_SUCCESS(rc))
    {
        PPDMIBASE  pBase;
        /*
         * Attach status driver (optional).
         */
        rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pBase, "Status Port");
        if (RT_SUCCESS(rc))
            pThisCC->pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
        else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        {
            Log(("%s/%d: warning: no driver attached to LUN #0!\n", pDevIns->pReg->szName, pDevIns->iInstance));
            rc = VINF_SUCCESS;
        }
        else
        {
            AssertMsgFailed(("Failed to attach to status driver. rc=%Rrc\n", rc));
            rc = PDMDEV_SET_ERROR(pDevIns, rc, N_("VGA cannot attach to status driver"));
        }
    }
    return rc;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) vgaRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, &pThis->CritSect);
    AssertRCReturn(rc, rc);

    if (pThis->fLegacyVgaEnabled)
    {
        /*
         * Set I/O port callbacks for this context.
         * We just copy the ring-3 registration bits and remove the '&' before the handle.
         */
# define REG_PORT(a_uPort, a_cPorts, a_pfnWrite, a_pfnRead, a_szDesc, a_hIoPort) do { \
            rc = PDMDevHlpIoPortSetUpContext(pDevIns, a_hIoPort, a_pfnWrite, a_pfnRead, NULL /*pvUser*/); \
            AssertRCReturn(rc, rc); \
        } while (0)

        REG_PORT(0x3c0,  2, vgaIoPortArWrite,       vgaIoPortArRead,        "Attribute Controller",     pThis->hIoPortAr);
        REG_PORT(0x3c2,  1, vgaIoPortMsrWrite,      vgaIoPortSt00Read,      "MSR / ST00",               pThis->hIoPortMsrSt00);
        REG_PORT(0x3c3,  1, vgaIoPortUnusedWrite,   vgaIoPortUnusedRead,    "0x3c3",                    pThis->hIoPort3c3);
        REG_PORT(0x3c4,  2, vgaIoPortSrWrite,       vgaIoPortSrRead,        "Sequencer",                pThis->hIoPortSr);
        REG_PORT(0x3c6,  4, vgaIoPortDacWrite,      vgaIoPortDacRead,       "DAC",                      pThis->hIoPortDac);
        REG_PORT(0x3ca,  4, vgaIoPortPosWrite,      vgaIoPortPosRead,       "Graphics Position", /*?*/  pThis->hIoPortPos);
        REG_PORT(0x3ce,  2, vgaIoPortGrWrite,       vgaIoPortGrRead,        "Graphics Controller",      pThis->hIoPortGr);

        REG_PORT(0x3b4,  2, vgaIoPortMdaCrtWrite,   vgaIoPortMdaCrtRead,    "MDA CRT control",          pThis->hIoPortMdaCrt);
        REG_PORT(0x3ba,  1, vgaIoPortMdaFcrWrite,   vgaIoPortMdaStRead,     "MDA feature/status",       pThis->hIoPortMdaFcrSt);
        REG_PORT(0x3d4,  2, vgaIoPortCgaCrtWrite,   vgaIoPortCgaCrtRead,    "CGA CRT control",          pThis->hIoPortCgaCrt);
        REG_PORT(0x3da,  1, vgaIoPortCgaFcrWrite,   vgaIoPortCgaStRead,     "CGA Feature / status",     pThis->hIoPortCgaFcrSt);

# ifdef CONFIG_BOCHS_VBE
        REG_PORT(0x1ce,  1, vgaIoPortWriteVbeIndex, vgaIoPortReadVbeIndex,  "VBE Index",                pThis->hIoPortVbeIndex);
        REG_PORT(0x1cf,  1, vgaIoPortWriteVbeData,  vgaIoPortReadVbeData,   "VBE Data",                 pThis->hIoPortVbeData);
# endif /* CONFIG_BOCHS_VBE */

# undef REG_PORT

        /* BIOS port: */
        rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortBios, vgaIoPortWriteBios, vgaIoPortReadBios, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);

        /*
         * MMIO.
         */
        rc = PDMDevHlpMmioSetUpContextEx(pDevIns, pThis->hMmioLegacy, vgaMmioWrite, vgaMmioRead, vgaMmioFill, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
    }

# ifdef VBOX_WITH_VMSVGA
    if (pThis->hIoPortVmSvga != NIL_IOMIOPORTHANDLE)
    {
        AssertReturn(pThis->fVMSVGAEnabled, VERR_INVALID_STATE);
        rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortVmSvga, vmsvgaIOWrite, vmsvgaIORead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
    }
    else
        AssertReturn(!pThis->fVMSVGAEnabled || pThis->fVmSvga3, VERR_INVALID_STATE);

    if (pThis->hMmioSvga3 != NIL_IOMMMIOHANDLE)
    {
        AssertReturn(pThis->fVMSVGAEnabled && pThis->fVmSvga3, VERR_INVALID_STATE);
        rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmioSvga3, vmsvga3MmioWrite, vmsvga3MmioRead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
    }
    else
        AssertReturn(!pThis->fVmSvga3, VERR_INVALID_STATE);
# endif

    /*
     * Map the start of the VRAM into this context.
     */
# if defined(VBOX_WITH_2X_4GB_ADDR_SPACE) || (defined(IN_RING0) && defined(VGA_WITH_PARTIAL_RING0_MAPPING))
    rc = PDMDevHlpMmio2SetUpContext(pDevIns, pThis->hMmio2VRam, 0 /* off */, VGA_MAPPING_SIZE, (void **)&pThisCC->pbVRam);
    AssertLogRelMsgRCReturn(rc, ("PDMDevHlpMmio2SetUpContext(,VRAM,0,%#x,) -> %Rrc\n", VGA_MAPPING_SIZE, rc), rc);
# endif

    /*
     * Map the first page of the VMSVGA FIFO into this context (not raw-mode).
     * We currently only access SVGA_FIFO_MIN, SVGA_FIFO_PITCHLOCK, and SVGA_FIFO_BUSY.
     */
# if defined(VBOX_WITH_VMSVGA) && !defined(IN_RC)
    AssertCompile((RT_MAX(SVGA_FIFO_MIN, RT_MAX(SVGA_FIFO_PITCHLOCK, SVGA_FIFO_BUSY)) + 1) * sizeof(uint32_t) < GUEST_PAGE_SIZE);
    if (   pThis->fVMSVGAEnabled
        && !pThis->fVmSvga3)
    {
        rc = PDMDevHlpMmio2SetUpContext(pDevIns, pThis->hMmio2VmSvgaFifo, 0 /* off */, GUEST_PAGE_SIZE,
                                        (void **)&pThisCC->svga.pau32FIFO);
        AssertLogRelMsgRCReturn(rc, ("PDMDevHlpMapMMIO2IntoR0(%#x,) -> %Rrc\n", pThis->svga.cbFIFO, rc), rc);
    }
    else
        AssertReturn(pThis->hMmio2VmSvgaFifo == NIL_PGMMMIO2HANDLE, VERR_INVALID_STATE);
# endif

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceVga =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "vga",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_GRAPHICS,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(VGASTATE),
    /* .cbInstanceCC = */           sizeof(VGASTATECC),
    /* .cbInstanceRC = */           sizeof(VGASTATERC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "VGA Adaptor with VESA extensions.",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           vgaR3Construct,
    /* .pfnDestruct = */            vgaR3Destruct,
    /* .pfnRelocate = */            vgaR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             vgaR3PowerOn,
    /* .pfnReset = */               vgaR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              vgaR3Resume,
    /* .pfnAttach = */              vgaAttach,
    /* .pfnDetach = */              vgaDetach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            vgaR3PowerOff,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           vgaRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           vgaRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

/*
 * Local Variables:
 *   nuke-trailing-whitespace-p:nil
 * End:
 */
