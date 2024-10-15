/* $Id: DevVGA.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevVGA - VBox VGA/VESA device, internal header.
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
 * QEMU internal VGA defines.
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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

#ifndef VBOX_INCLUDED_SRC_Graphics_DevVGA_h
#define VBOX_INCLUDED_SRC_Graphics_DevVGA_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBoxVideoVBE.h>
#include <VBoxVideoVBEPrivate.h>

#ifdef VBOX_WITH_HGSMI
# include "HGSMI/HGSMIHost.h"
#endif /* VBOX_WITH_HGSMI */
#include "DevVGASavedState.h"

#ifdef VBOX_WITH_VMSVGA
# include "DevVGA-SVGA.h"
#else
# include <VBox/vmm/pdmifs.h>
# include <VBox/vmm/stam.h>
#endif

#include <VBox/param.h> /* For VRAM ranges. */
#ifdef VBOX_WITH_VMSVGA
/* Make sure the param.h copy of the SVGA VRAM sizes matches the originals. */
AssertCompile(VBOX_SVGA_VRAM_MIN_SIZE    == SVGA_VRAM_MIN_SIZE);
AssertCompile(VBOX_SVGA_VRAM_MIN_SIZE_3D == SVGA_VRAM_MIN_SIZE_3D);
AssertCompile(VBOX_SVGA_VRAM_MAX_SIZE    == SVGA_VRAM_MAX_SIZE);
#endif

#include <iprt/list.h>


/** Use VBE bytewise I/O. Only needed for Windows Longhorn/Vista betas and backwards compatibility. */
#define VBE_BYTEWISE_IO

/** @name Macros dealing with partial ring-0/raw-mode VRAM mappings.
 * @{ */
/** The size of the VGA ring-0 and raw-mode mapping.
 *
 * This is supposed to be all the VGA memory accessible to the guest.
 * The initial value was 256KB but NTAllInOne.iso appears to access more
 * thus the limit was upped to 512KB.
 *
 * @todo Someone with some VGA knowhow should make a better guess at this value.
 */
#define VGA_MAPPING_SIZE    _512K
/** Enables partially mapping the VRAM into ring-0 rather than using the ring-3.
 * The VGA_MAPPING_SIZE define sets the number of bytes that will be mapped. */
#define VGA_WITH_PARTIAL_RING0_MAPPING

/**
 * Check buffer if an VRAM offset is within the right range or not.
 */
#if defined(IN_RC) || defined(VBOX_WITH_2X_4GB_ADDR_SPACE_IN_R0) || (defined(IN_RING0) && defined(VGA_WITH_PARTIAL_RING0_MAPPING))
# define VERIFY_VRAM_WRITE_OFF_RETURN(pThis, off) \
    do { \
        if ((off) < VGA_MAPPING_SIZE) \
            RT_UNTRUSTED_VALIDATED_FENCE(); \
        else \
        { \
            AssertMsgReturn((off) < (pThis)->vram_size, ("%RX32 !< %RX32\n", (uint32_t)(off), (pThis)->vram_size), VINF_SUCCESS); \
            Log2(("%Rfn[%d]: %RX32 -> R3\n", __PRETTY_FUNCTION__, __LINE__, (off))); \
            return VINF_IOM_R3_MMIO_WRITE; \
        } \
    } while (0)
#else
# define VERIFY_VRAM_WRITE_OFF_RETURN(pThis, off) \
    do { \
       AssertMsgReturn((off) < (pThis)->vram_size, ("%RX32 !< %RX32\n", (uint32_t)(off), (pThis)->vram_size), VINF_SUCCESS); \
       RT_UNTRUSTED_VALIDATED_FENCE(); \
    } while (0)
#endif

/**
 * Check buffer if an VRAM offset is within the right range or not.
 */
#if defined(IN_RC) || defined(VBOX_WITH_2X_4GB_ADDR_SPACE_IN_R0) || (defined(IN_RING0) && defined(VGA_WITH_PARTIAL_RING0_MAPPING))
# define VERIFY_VRAM_READ_OFF_RETURN(pThis, off, rcVar) \
    do { \
        if ((off) < VGA_MAPPING_SIZE) \
            RT_UNTRUSTED_VALIDATED_FENCE(); \
        else \
        { \
            AssertMsgReturn((off) < (pThis)->vram_size, ("%RX32 !< %RX32\n", (uint32_t)(off), (pThis)->vram_size), 0xff); \
            Log2(("%Rfn[%d]: %RX32 -> R3\n", __PRETTY_FUNCTION__, __LINE__, (off))); \
            (rcVar) = VINF_IOM_R3_MMIO_READ; \
            return 0; \
        } \
    } while (0)
#else
# define VERIFY_VRAM_READ_OFF_RETURN(pThis, off, rcVar) \
    do { \
        AssertMsgReturn((off) < (pThis)->vram_size, ("%RX32 !< %RX32\n", (uint32_t)(off), (pThis)->vram_size), 0xff); \
        RT_UNTRUSTED_VALIDATED_FENCE(); \
        NOREF(rcVar); \
    } while (0)
#endif
/** @} */


#define MSR_COLOR_EMULATION 0x01
#define MSR_PAGE_SELECT     0x20

#define ST01_V_RETRACE      0x08
#define ST01_DISP_ENABLE    0x01

/* bochs VBE support */
#define CONFIG_BOCHS_VBE

#ifdef CONFIG_BOCHS_VBE

/* Cross reference with <VBoxVideoVBE.h> */
#define VBE_DISPI_INDEX_NB_SAVED        0xb /* Old number of saved registers (vbe_regs array, see vga_load) */
#define VBE_DISPI_INDEX_NB              0xd /* Total number of VBE registers */

#define VGA_STATE_COMMON_BOCHS_VBE              \
    uint16_t vbe_index;                         \
    uint16_t vbe_regs[VBE_DISPI_INDEX_NB];      \
    uint16_t alignment[2]; /* pad to 64 bits */ \
    uint32_t vbe_start_addr;                    \
    uint32_t vbe_line_offset;                   \
    uint32_t vbe_bank_max;

#else

#define VGA_STATE_COMMON_BOCHS_VBE

#endif /* !CONFIG_BOCHS_VBE */

#define CH_ATTR_SIZE (160 * 100)
#define VGA_MAX_HEIGHT VBE_DISPI_MAX_YRES

typedef struct vga_retrace_s {
    unsigned    frame_cclks;    /* Character clocks per frame. */
    unsigned    frame_ns;       /* Frame duration in ns. */
    unsigned    cclk_ns;        /* Character clock duration in ns. */
    unsigned    vb_start;       /* Vertical blanking start (scanline). */
    unsigned    vb_end;         /* Vertical blanking end (scanline). */
    unsigned    vb_end_ns;      /* Vertical blanking end time (length) in ns. */
    unsigned    vs_start;       /* Vertical sync start (scanline). */
    unsigned    vs_end;         /* Vertical sync end (scanline). */
    unsigned    vs_start_ns;    /* Vertical sync start time in ns. */
    unsigned    vs_end_ns;      /* Vertical sync end time in ns. */
    unsigned    h_total;        /* Horizontal total (cclks per scanline). */
    unsigned    h_total_ns;     /* Scanline duration in ns. */
    unsigned    hb_start;       /* Horizontal blanking start (cclk). */
    unsigned    hb_end;         /* Horizontal blanking end (cclk). */
    unsigned    hb_end_ns;      /* Horizontal blanking end time (length) in ns. */
    unsigned    v_freq_hz;      /* Vertical refresh rate to emulate. */
} vga_retrace_s;

#ifndef VBOX
#define VGA_STATE_COMMON                                                \
    unsigned long vram_offset;                                          \
    unsigned int vram_size;                                             \
    uint32_t latch;                                                     \
    uint8_t sr_index;                                                   \
    uint8_t sr[256];                                                    \
    uint8_t gr_index;                                                   \
    uint8_t gr[256];                                                    \
    uint8_t ar_index;                                                   \
    uint8_t ar[21];                                                     \
    int ar_flip_flop;                                                   \
    uint8_t cr_index;                                                   \
    uint8_t cr[256]; /* CRT registers */                                \
    uint8_t msr; /* Misc Output Register */                             \
    uint8_t fcr; /* Feature Control Register */                         \
    uint8_t st00; /* status 0 */                                        \
    uint8_t st01; /* status 1 */                                        \
    uint8_t dac_state;                                                  \
    uint8_t dac_sub_index;                                              \
    uint8_t dac_read_index;                                             \
    uint8_t dac_write_index;                                            \
    uint8_t dac_cache[3]; /* used when writing */                       \
    uint8_t palette[768];                                               \
    int32_t bank_offset;                                                \
    int (*get_bpp)(struct VGAState *s);                                 \
    void (*get_offsets)(struct VGAState *s,                             \
                        uint32_t *pline_offset,                         \
                        uint32_t *pstart_addr,                          \
                        uint32_t *pline_compare);                       \
    void (*get_resolution)(struct VGAState *s,                          \
                        int *pwidth,                                    \
                        int *pheight);                                  \
    VGA_STATE_COMMON_BOCHS_VBE                                          \
    /* display refresh support */                                       \
    DisplayState *ds;                                                   \
    uint32_t font_offsets[2];                                           \
    int graphic_mode;                                                   \
    uint8_t shift_control;                                              \
    uint8_t double_scan;                                                \
    uint32_t line_offset;                                               \
    uint32_t line_compare;                                              \
    uint32_t start_addr;                                                \
    uint32_t plane_updated;                                             \
    uint8_t last_cw, last_ch;                                           \
    uint32_t last_width, last_height; /* in chars or pixels */          \
    uint32_t last_scr_width, last_scr_height; /* in pixels */           \
    uint8_t cursor_start, cursor_end;                                   \
    uint32_t cursor_offset;                                             \
    unsigned int (*rgb_to_pixel)(unsigned int r,                        \
                                 unsigned int g, unsigned b);           \
    /* hardware mouse cursor support */                                 \
    uint32_t invalidated_y_table[VGA_MAX_HEIGHT / 32];                  \
    void (*cursor_invalidate)(struct VGAState *s);                      \
    void (*cursor_draw_line)(struct VGAState *s, uint8_t *d, int y);    \
    /* tell for each page if it has been updated since the last time */ \
    uint32_t last_palette[256];                                         \
    uint32_t last_ch_attr[CH_ATTR_SIZE]; /* XXX: make it dynamic */

#else /* VBOX */

/* bird: Since we've changed types, reordered members, done alignment
         paddings and more, VGA_STATE_COMMON was added directly to the
         struct to make it more readable and easier to handle. */

struct VGAState;
typedef int FNGETBPP(struct VGAState *s);
typedef void FNGETOFFSETS(struct VGAState *s, uint32_t *pline_offset, uint32_t *pstart_addr, uint32_t *pline_compare);
typedef void FNGETRESOLUTION(struct VGAState *s, int *pwidth, int *pheight);
typedef unsigned int FNRGBTOPIXEL(unsigned int r, unsigned int g, unsigned b);
typedef void FNCURSORINVALIDATE(struct VGAState *s);
typedef void FNCURSORDRAWLINE(struct VGAState *s, uint8_t *d, int y);

#endif /* VBOX */

#ifdef VBOX_WITH_VDMA
typedef struct VBOXVDMAHOST *PVBOXVDMAHOST;
#endif

#ifdef VBOX_WITH_VIDEOHWACCEL
#define VBOX_VHWA_MAX_PENDING_COMMANDS 1000

typedef struct _VBOX_VHWA_PENDINGCMD
{
    RTLISTNODE Node;
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_GUEST *pCommand;
} VBOX_VHWA_PENDINGCMD;
#endif


/**
 * The shared VGA state data.
 */
typedef struct VGAState
{
    uint32_t vram_size;
    uint32_t latch;
    uint8_t sr_index;
    uint8_t sr[256];
    uint8_t gr_index;
    uint8_t gr[256];
    uint8_t ar_index;
    uint8_t ar[21];
    int32_t ar_flip_flop;
    uint8_t cr_index;
    uint8_t cr[256]; /* CRT registers */
    uint8_t msr; /* Misc Output Register */
    uint8_t fcr; /* Feature Control Register */
    uint8_t st00; /* status 0 */
    uint8_t st01; /* status 1 */
    uint8_t dac_state;
    uint8_t dac_sub_index;
    uint8_t dac_read_index;
    uint8_t dac_write_index;
    uint8_t dac_cache[3]; /* used when writing */
    uint8_t palette[768];
    int32_t bank_offset;
    VGA_STATE_COMMON_BOCHS_VBE
    /* display refresh support */
    uint32_t font_offsets[2];
    int32_t graphic_mode;
    uint8_t shift_control;
    uint8_t double_scan;
    uint8_t padding1[2];
    uint32_t line_offset;
    uint32_t vga_addr_mask;
    uint32_t padding1a;
    uint32_t line_compare;
    uint32_t start_addr;
    uint32_t plane_updated;
    uint8_t last_cw, last_ch;
    uint8_t last_uline;                                                 \
    bool last_blink;                                                    \
    uint32_t last_width, last_height; /* in chars or pixels */
    uint32_t last_scr_width, last_scr_height; /* in pixels */
    uint32_t last_bpp;
    uint8_t cursor_start, cursor_end;
    bool last_cur_blink, last_chr_blink;
    uint32_t cursor_offset;
    /** hardware mouse cursor support */
    uint32_t invalidated_y_table[VGA_MAX_HEIGHT / 32];
    /** tell for each page if it has been updated since the last time */
    uint32_t last_palette[256];
    uint32_t last_ch_attr[CH_ATTR_SIZE]; /* XXX: make it dynamic */

    /** end-of-common-state-marker */
    uint32_t                    u32Marker;

    /** Refresh timer handle - HC. */
    TMTIMERHANDLE               hRefreshTimer;

#ifdef VBOX_WITH_VMSVGA
    VMSVGASTATE                 svga;
#endif

    /** The number of monitors. */
    uint32_t                    cMonitors;
    /** Current refresh timer interval. */
    uint32_t                    cMilliesRefreshInterval;
    /** Bitmap tracking dirty pages. */
    uint64_t                    bmDirtyBitmap[VGA_VRAM_MAX / GUEST_PAGE_SIZE / 64];
    /** Bitmap tracking which VGA memory pages in the 0xa0000-0xbffff region has
     * been remapped to allow direct access.
     * @note It's quite possible that mapping in the 0xb0000-0xbffff isn't possible,
     *       but we're playing safe and cover the whole VGA MMIO region here. */
    uint32_t                    bmPageRemappedVGA;

    /** Flag indicating that there are dirty bits. This is used to optimize the handler resetting. */
    bool                        fHasDirtyBits;
    /** Whether to render the guest VRAM to the framebuffer memory. False only for some LFB modes. */
    bool                        fRenderVRAM;
    /** Whether 3D is enabled for the VM. */
    bool                        f3DEnabled;
    /** Set if state has been restored. */
    bool                        fStateLoaded;
    /** Flag whether to expose the legacy VGA interface to the guest. */
    bool                        fLegacyVgaEnabled;
#ifdef VBOX_WITH_VMSVGA
    /* Whether the SVGA emulation is enabled or not. */
    bool                        fVMSVGAEnabled;
    bool                        fVMSVGA10;
    bool                        fVMSVGAPciId;
    bool                        fVMSVGAPciBarLayout;
    /** Flag whether the SVGA3 interface is exposed to the guest. */
    bool                        fVmSvga3;
#else
    bool                        afPadding4[5];
#endif

    struct {
        uint32_t                    u32Padding1;
        uint32_t                    iVRAM;
#ifdef VBOX_WITH_VMSVGA
        uint32_t                    iIO;
        uint32_t                    iFIFO;
#endif
    } pciRegions;

    /** The physical address the VRAM was assigned. */
    RTGCPHYS                    GCPhysVRAM;
    /** The critical section protect the instance data. */
    PDMCRITSECT                 CritSect;

    /* Keep track of ring 0 latched accesses to the VGA MMIO memory. */
    uint64_t                    u64LastLatchedAccess;
    uint32_t                    cLatchAccesses;
    uint16_t                    uMaskLatchAccess;
    uint16_t                    iMask;

#ifdef VBE_BYTEWISE_IO
    /** VBE read/write data/index flags */
    uint8_t                     fReadVBEData;
    uint8_t                     fWriteVBEData;
    uint8_t                     fReadVBEIndex;
    uint8_t                     fWriteVBEIndex;
    /** VBE write data/index one byte buffer */
    uint8_t                     cbWriteVBEData;
    uint8_t                     cbWriteVBEIndex;
    /** VBE Extra Data write address one byte buffer */
    uint8_t                     cbWriteVBEExtraAddress;
    uint8_t                     Padding5;
#endif

    /** Retrace emulation state */
    bool                        fRealRetrace;
    bool                        Padding6[HC_ARCH_BITS == 64 ? 7 : 3];
    vga_retrace_s               retrace_state;

#ifdef VBOX_WITH_HGSMI
    /** Base port in the assigned PCI I/O space. */
    RTIOPORT                    IOPortBase;
# ifdef VBOX_WITH_WDDM
    uint8_t                     Padding10[2];
    /** Specifies guest driver caps, i.e. whether it can handle IRQs from the
     * adapter, the way it can handle async HGSMI command completion, etc. */
    uint32_t                    fGuestCaps;
    uint32_t                    fScanLineCfg;
    uint32_t                    Padding11;
# else
    uint8_t                     Padding11[14];
# endif

    /** The critical section serializes the HGSMI IRQ setting/clearing. */
    PDMCRITSECT                 CritSectIRQ;
    /** VBVARaiseIRQ flags which were set when the guest was still processing previous IRQ. */
    uint32_t                    fu32PendingGuestFlags;
    uint32_t                    Padding12;
#endif /* VBOX_WITH_HGSMI */

    PDMLED Led3D;

    struct {
        volatile uint32_t cPending;
        uint32_t Padding1;
        union
        {
            RTLISTNODE PendingList;
            /* make sure the structure sized cross different contexts correctly */
            struct
            {
                R3PTRTYPE(void *) dummy1;
                R3PTRTYPE(void *) dummy2;
            } dummy;
        };
    } pendingVhwaCommands;

    /** The MMIO handle of the legacy graphics buffer/regs at 0xa0000-0xbffff. */
    PGMMMIO2HANDLE              hMmioLegacy;

    /** @name I/O ports for range 0x3c0-3cf.
     * @{ */
    IOMIOPORTHANDLE             hIoPortAr;
    IOMIOPORTHANDLE             hIoPortMsrSt00;
    IOMIOPORTHANDLE             hIoPort3c3;
    IOMIOPORTHANDLE             hIoPortSr;
    IOMIOPORTHANDLE             hIoPortDac;
    IOMIOPORTHANDLE             hIoPortPos;
    IOMIOPORTHANDLE             hIoPortGr;
    /** @} */

    /** @name I/O ports for MDA 0x3b0-0x3bf (sparse)
     * @{ */
    IOMIOPORTHANDLE             hIoPortMdaCrt;
    IOMIOPORTHANDLE             hIoPortMdaFcrSt;
    /** @} */

    /** @name I/O ports for CGA 0x3d0-0x3df (sparse)
     * @{ */
    IOMIOPORTHANDLE             hIoPortCgaCrt;
    IOMIOPORTHANDLE             hIoPortCgaFcrSt;
    /** @} */

#ifdef VBOX_WITH_HGSMI
    /** @name I/O ports for HGSMI 0x3b0-03b3 and 0x3d0-03d3 (ring-3 only)
     * @{ */
    IOMIOPORTHANDLE             hIoPortHgsmiHost;
    IOMIOPORTHANDLE             hIoPortHgsmiGuest;
    /** @} */
#endif

    /** @name I/O ports for Boch VBE 0x1ce-0x1cf
     *  @{ */
    IOMIOPORTHANDLE             hIoPortVbeIndex;
    IOMIOPORTHANDLE             hIoPortVbeData;
    /** @} */

    /** The BIOS printf I/O port. */
    IOMIOPORTHANDLE             hIoPortBios;
    /** The VBE extra data I/O port. */
    IOMIOPORTHANDLE             hIoPortVbeExtra;
    /** The logo command I/O port. */
    IOMIOPORTHANDLE             hIoPortCmdLogo;

#ifdef VBOX_WITH_VMSVGA
    /** VMSVGA: I/O port PCI region. */
    IOMIOPORTHANDLE             hIoPortVmSvga;
    /** VMSVGA3: MMIO PCI region for the registers. */
    IOMMMIOHANDLE               hMmioSvga3;
    /** VMSVGA: The MMIO2 handle of the FIFO PCI region. */
    PGMMMIO2HANDLE              hMmio2VmSvgaFifo;
#endif
    /** The MMIO2 handle of the VRAM. */
    PGMMMIO2HANDLE              hMmio2VRam;

    STAMPROFILE                 StatRZMemoryRead;
    STAMPROFILE                 StatR3MemoryRead;
    STAMPROFILE                 StatRZMemoryWrite;
    STAMPROFILE                 StatR3MemoryWrite;
    STAMCOUNTER                 StatMapPage;            /**< Counts IOMMmioMapMmio2Page calls.  */
    STAMCOUNTER                 StatMapReset;           /**< Counts IOMMmioResetRegion calls.  */
    STAMCOUNTER                 StatUpdateDisp;         /**< Counts vgaPortUpdateDisplay calls.  */
#ifdef VBOX_WITH_HGSMI
    STAMCOUNTER                 StatHgsmiMdaCgaAccesses;
#endif
} VGAState;
#ifdef VBOX
/** VGA state. */
typedef VGAState VGASTATE;
/** Pointer to the VGA state. */
typedef VGASTATE *PVGASTATE;
AssertCompileMemberAlignment(VGASTATE, bank_offset, 8);
AssertCompileMemberAlignment(VGASTATE, font_offsets, 8);
AssertCompileMemberAlignment(VGASTATE, last_ch_attr, 8);
AssertCompileMemberAlignment(VGASTATE, u32Marker, 8);
AssertCompile(sizeof(uint64_t)/*bmPageMapBitmap*/ >= (_64K / GUEST_PAGE_SIZE / 8));
#endif


/**
 * The VGA state data for ring-3 context.
 */
typedef struct VGASTATER3
{
    R3PTRTYPE(uint8_t *)        pbVRam;
    R3PTRTYPE(FNGETBPP *)       get_bpp;
    R3PTRTYPE(FNGETOFFSETS *)   get_offsets;
    R3PTRTYPE(FNGETRESOLUTION *) get_resolution;
    R3PTRTYPE(FNRGBTOPIXEL *)   rgb_to_pixel;
    R3PTRTYPE(FNCURSORINVALIDATE *) cursor_invalidate;
    R3PTRTYPE(FNCURSORDRAWLINE *) cursor_draw_line;

    /** Pointer to the device instance.
     * @note Only for getting our bearings in interface methods.  */
    PPDMDEVINSR3                pDevIns;
#ifdef VBOX_WITH_HGSMI
    R3PTRTYPE(PHGSMIINSTANCE)   pHGSMI;
#endif
#ifdef VBOX_WITH_VDMA
    R3PTRTYPE(PVBOXVDMAHOST)    pVdma;
#endif

    /** LUN\#0: The display port base interface. */
    PDMIBASE                    IBase;
    /** LUN\#0: The display port interface. */
    PDMIDISPLAYPORT             IPort;
#ifdef VBOX_WITH_HGSMI
    /** LUN\#0: VBVA callbacks interface */
    PDMIDISPLAYVBVACALLBACKS    IVBVACallbacks;
#endif
    /** Status LUN: Leds interface. */
    PDMILEDPORTS                ILeds;

    /** Pointer to base interface of the driver. */
    R3PTRTYPE(PPDMIBASE)        pDrvBase;
    /** Pointer to display connector interface of the driver. */
    R3PTRTYPE(PPDMIDISPLAYCONNECTOR) pDrv;

    /** Status LUN: Partner of ILeds. */
    R3PTRTYPE(PPDMILEDCONNECTORS) pLedsConnector;

#ifdef VBOX_WITH_VMSVGA
    /** The VMSVGA ring-3 state. */
    VMSVGASTATER3               svga;
#endif

    /** The VGA BIOS ROM data. */
    R3PTRTYPE(uint8_t *)        pbVgaBios;
    /** The size of the VGA BIOS ROM. */
    uint64_t                    cbVgaBios;
    /** The name of the VGA BIOS ROM file. */
    R3PTRTYPE(char *)           pszVgaBiosFile;

    /** @name Logo data
     * @{ */
    /** Current logo data offset. */
    uint32_t                    offLogoData;
    /** The size of the BIOS logo data. */
    uint32_t                    cbLogo;
    /** Current logo command. */
    uint16_t                    LogoCommand;
    /** Bitmap width. */
    uint16_t                    cxLogo;
    /** Bitmap height. */
    uint16_t                    cyLogo;
    /** Bitmap planes. */
    uint16_t                    cLogoPlanes;
    /** Bitmap depth. */
    uint16_t                    cLogoBits;
    /** Bitmap compression. */
    uint16_t                    LogoCompression;
    /** Bitmap colors used. */
    uint16_t                    cLogoUsedColors;
    /** Palette size. */
    uint16_t                    cLogoPalEntries;
    /** Clear screen flag. */
    uint8_t                     fLogoClearScreen;
    bool                        fBootMenuInverse;
    uint8_t                     Padding8[6];
    /** Palette data. */
    uint32_t                    au32LogoPalette[256];
    /** The BIOS logo data. */
    R3PTRTYPE(uint8_t *)        pbLogo;
    /** The name of the logo file. */
    R3PTRTYPE(char *)           pszLogoFile;
    /** Bitmap image data. */
    R3PTRTYPE(uint8_t *)        pbLogoBitmap;
    /** @} */

    /** @name VBE extra data (modes)
     * @{ */
    /** The VBE BIOS extra data. */
    R3PTRTYPE(uint8_t *)        pbVBEExtraData;
    /** The size of the VBE BIOS extra data. */
    uint16_t                    cbVBEExtraData;
    /** The VBE BIOS current memory address. */
    uint16_t                    u16VBEExtraAddress;
    uint16_t                    Padding7[2];
    /** @} */

} VGASTATER3;
/** Pointer to the ring-3 VGA state. */
typedef VGASTATER3 *PVGASTATER3;


/**
 * The VGA state data for ring-0 context.
 */
typedef struct VGASTATER0
{
    /** The R0 vram pointer. */
    R0PTRTYPE(uint8_t *)        pbVRam;
#ifdef VBOX_WITH_VMSVGA
    /** The VMSVGA ring-0 state. */
    VMSVGASTATER0               svga;
#endif
} VGASTATER0;
/** Pointer to the ring-0 VGA state. */
typedef VGASTATER0 *PVGASTATER0;


/**
 * The VGA state data for raw-mode context.
 */
typedef struct VGASTATERC
{
    /** Pointer to the RC vram mapping. */
    RCPTRTYPE(uint8_t *)        pbVRam;
} VGASTATERC;
/** Pointer to the raw-mode VGA state. */
typedef VGASTATERC *PVGASTATERC;


/** The VGA state for the current context. */
typedef CTX_SUFF(VGASTATE) VGASTATECC;
/** Pointer to the VGA state for the current context. */
typedef CTX_SUFF(PVGASTATE) PVGASTATECC;



/** VBE Extra Data. */
typedef VBEHeader VBEHEADER;
/** Pointer to the VBE Extra Data. */
typedef VBEHEADER *PVBEHEADER;

#if !defined(VBOX) || defined(IN_RING3)
static inline int c6_to_8(int v)
{
    int b;
    v &= 0x3f;
    b = v & 1;
    return (v << 2) | (b << 1) | b;
}
#endif /* !VBOX || IN_RING3 */


#ifdef VBOX_WITH_HGSMI
int     VBVAInit(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC);
void    VBVADestroy(PVGASTATECC pThisCC);
int     VBVAUpdateDisplay(PVGASTATE pThis, PVGASTATECC pThisCC);
void    VBVAReset(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC);
void    VBVAOnVBEChanged(PVGASTATE pThis, PVGASTATECC pThisCC);
void    VBVAOnResume(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC);

bool    VBVAIsPaused(PVGASTATECC pThisCC);
#ifdef UNUSED_FUNCTION
bool    VBVAIsEnabled(PVGASTATECC pThisCC);
#endif

void    VBVARaiseIrq(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t fFlags);

int     VBVAInfoScreen(PVGASTATE pThis, const VBVAINFOSCREEN RT_UNTRUSTED_VOLATILE_HOST *pScreen);
#ifdef UNUSED_FUNCTION
int     VBVAGetInfoViewAndScreen(PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t u32ViewIndex,
                                 VBVAINFOVIEW *pView, VBVAINFOSCREEN *pScreen);
#endif

# ifdef VBOX_WITH_VIDEOHWACCEL
DECLCALLBACK(int) vbvaR3VHWACommandCompleteAsync(PPDMIDISPLAYVBVACALLBACKS pInterface,
                                                 VBOXVHWACMD RT_UNTRUSTED_VOLATILE_GUEST *pCmd);
int vbvaVHWAConstruct(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC);

void vbvaTimerCb(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC);

int vboxVBVASaveStatePrep(PPDMDEVINS pDevIns);
int vboxVBVASaveStateDone(PPDMDEVINS pDevIns);
# endif

int vboxVBVASaveStateExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM);
int vboxVBVALoadStateExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t u32Version);
int vboxVBVALoadStateDone(PPDMDEVINS pDevIns);

DECLCALLBACK(int) vbvaR3PortSendModeHint(PPDMIDISPLAYPORT pInterface, uint32_t cx, uint32_t cy, uint32_t cBPP,
                                         uint32_t cDisplay, uint32_t dx, uint32_t dy, uint32_t fEnabled, uint32_t fNotifyGuest);

# ifdef VBOX_WITH_VDMA
typedef struct VBOXVDMAHOST *PVBOXVDMAHOST;
int vboxVDMAConstruct(PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t cPipeElements);
void vboxVDMADestruct(PVBOXVDMAHOST pVdma);
void vboxVDMAReset(PVBOXVDMAHOST pVdma);
void vboxVDMAControl(PVBOXVDMAHOST pVdma, VBOXVDMA_CTL RT_UNTRUSTED_VOLATILE_GUEST *pCmd, uint32_t cbCmd);
void vboxVDMACommand(PVBOXVDMAHOST pVdma, VBOXVDMACBUF_DR RT_UNTRUSTED_VOLATILE_GUEST *pCmd, uint32_t cbCmd);
int vboxVDMASaveStateExecPrep(struct VBOXVDMAHOST *pVdma);
int vboxVDMASaveStateExecDone(struct VBOXVDMAHOST *pVdma);
int vboxVDMASaveStateExecPerform(PCPDMDEVHLPR3 pHlp, struct VBOXVDMAHOST *pVdma, PSSMHANDLE pSSM);
int vboxVDMASaveLoadExecPerform(PCPDMDEVHLPR3 pHlp, struct VBOXVDMAHOST *pVdma, PSSMHANDLE pSSM, uint32_t u32Version);
int vboxVDMASaveLoadDone(struct VBOXVDMAHOST *pVdma);
# endif /* VBOX_WITH_VDMA */

#endif /* VBOX_WITH_HGSMI */

# ifdef VBOX_WITH_VMSVGA
int vgaR3UnregisterVRAMHandler(PPDMDEVINS pDevIns, PVGASTATE pThis);
int vgaR3RegisterVRAMHandler(PPDMDEVINS pDevIns, PVGASTATE pThis, uint64_t cbFrameBuffer);
int vgaR3UpdateDisplay(PVGASTATE pThis, unsigned xStart, unsigned yStart, unsigned width, unsigned height);
# endif

#ifndef VBOX
void vga_common_init(VGAState *s, DisplayState *ds, uint8_t *vga_ram_base,
                     unsigned long vga_ram_offset, int vga_ram_size);
uint32_t vga_mem_readb(void *opaque, target_phys_addr_t addr);
void vga_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val);
void vga_invalidate_scanlines(VGAState *s, int y1, int y2);

void vga_draw_cursor_line_8(uint8_t *d1, const uint8_t *src1,
                            int poffset, int w,
                            unsigned int color0, unsigned int color1,
                            unsigned int color_xor);
void vga_draw_cursor_line_16(uint8_t *d1, const uint8_t *src1,
                             int poffset, int w,
                             unsigned int color0, unsigned int color1,
                             unsigned int color_xor);
void vga_draw_cursor_line_32(uint8_t *d1, const uint8_t *src1,
                             int poffset, int w,
                             unsigned int color0, unsigned int color1,
                             unsigned int color_xor);

extern const uint8_t sr_mask[8];
extern const uint8_t gr_mask[16];
#endif /* !VBOX */

#endif /* !VBOX_INCLUDED_SRC_Graphics_DevVGA_h */

