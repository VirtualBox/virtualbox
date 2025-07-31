/* $Id: DevVGA-SVGA.h 110487 2025-07-30 17:55:38Z vitali.pelenjow@oracle.com $ */
/** @file
 * VMware SVGA device
 */
/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA_h
#define VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_WITH_VMSVGA
# error "VBOX_WITH_VMSVGA is not defined"
#endif

#define VMSVGA_USE_EMT_HALT_CODE

#define VMSVGA_WITH_PGM_LOCKING

#include <VBox/pci.h>
#include <VBox/vmm/pdmifs.h>
#include <VBox/vmm/pdmthread.h>
#include <VBox/vmm/stam.h>
#ifdef VMSVGA_USE_EMT_HALT_CODE
# include <VBox/vmm/vmapi.h>
# include <VBox/vmm/vmcpuset.h>
#endif

#include <iprt/avl.h>
#include <iprt/list.h>


/*
 * PCI device IDs.
 */
#ifndef PCI_VENDOR_ID_VMWARE
# define PCI_VENDOR_ID_VMWARE            0x15AD
#endif
#ifndef PCI_DEVICE_ID_VMWARE_SVGA2
# define PCI_DEVICE_ID_VMWARE_SVGA2      0x0405
#endif
#ifndef PCI_DEVICE_ID_VMWARE_SVGA3
# define PCI_DEVICE_ID_VMWARE_SVGA3      0x0406
#endif

/* For "svga_overlay.h" */
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

/* VMSVGA headers. */
#include "vmsvga_headers_begin.h"
#pragma pack(1) /* VMSVGA structures are '__packed'. */
#include <svga3d_caps.h>
#include <svga3d_reg.h>
#include <svga3d_shaderdefs.h>
#include <svga_escape.h>
#include <svga_overlay.h>
#include <vbsvga3d_dx.h>
#pragma pack()
#include "vmsvga_headers_end.h"

/**@def FLOAT_FMT_STR
 * Format string bits to go with FLOAT_FMT_ARGS. */
#define FLOAT_FMT_STR                  "%s%u.%06u"
/** @def FLOAT_FMT_ARGS
 * Format arguments for a float value, corresponding to FLOAT_FMT_STR.
 * @param   r       The floating point value to format.  */
#define FLOAT_FMT_ARGS(r)              (r) >= 0.0f ? "" : "-", (unsigned)RT_ABS(r) \
                                       , (unsigned)(RT_ABS((r) - (float)(unsigned)(r)) * 1000000.0f)

/* Deprecated commands. They are not included in the VMSVGA headers anymore. */
#define SVGA_CMD_RECT_FILL             2
#define SVGA_CMD_DISPLAY_CURSOR        20
#define SVGA_CMD_MOVE_CURSOR           21

/*
 * SVGA_CMD_RECT_FILL --
 *
 *    Fill a rectangular area in the the GFB, and copy the result
 *    to any screens which intersect it.
 *
 *    Deprecated?
 *
 * Availability:
 *    SVGA_CAP_RECT_FILL
 */

typedef
struct {
   uint32_t pixel;
   uint32_t destX;
   uint32_t destY;
   uint32_t width;
   uint32_t height;
} SVGAFifoCmdRectFill;

/*
 * SVGA_CMD_DISPLAY_CURSOR --
 *
 *    Turn the cursor on or off.
 *
 *    Deprecated.
 *
 * Availability:
 *    SVGA_CAP_CURSOR?
 */

typedef
struct {
   uint32_t id;             // Reserved, must be zero.
   uint32_t state;          // 0=off
} SVGAFifoCmdDisplayCursor;

/*
 * SVGA_CMD_MOVE_CURSOR --
 *
 *    Set the cursor position.
 *
 *    Deprecated.
 *
 * Availability:
 *    SVGA_CAP_CURSOR?
 */

typedef
struct {
    SVGASignedPoint     pos;
} SVGAFifoCmdMoveCursor;


/** Default FIFO size. */
#define VMSVGA_FIFO_SIZE                _2M
/** The old FIFO size. */
#define VMSVGA_FIFO_SIZE_OLD            _128K

/** Default scratch region size. */
#define VMSVGA_SCRATCH_SIZE             0x100
/** Surface memory available to the guest. */
#define VMSVGA_SURFACE_SIZE             (512*1024*1024)
/** Maximum GMR pages. */
#define VMSVGA_MAX_GMR_PAGES            0x100000
/** Maximum nr of GMR ids. */
#define VMSVGA_MAX_GMR_IDS              _8K
/** Maximum number of GMR descriptors.  */
#define VMSVGA_MAX_GMR_DESC_LOOP_COUNT  VMSVGA_MAX_GMR_PAGES

#define VMSVGA_VAL_UNINITIALIZED        (unsigned)-1

/** For validating X and width values.
 * The code assumes it's at least an order of magnitude less than UINT32_MAX. */
#define VMSVGA_MAX_X                    _1M
/** For validating Y and height values.
 * The code assumes it's at least an order of magnitude less than UINT32_MAX. */
#define VMSVGA_MAX_Y                    _1M

/* u32ActionFlags */
#define VMSVGA_ACTION_CHANGEMODE_BIT    0
#define VMSVGA_ACTION_CHANGEMODE        RT_BIT(VMSVGA_ACTION_CHANGEMODE_BIT)


#ifdef DEBUG
/* Enable to log FIFO register accesses. */
//# define DEBUG_FIFO_ACCESS
/* Enable to log GMR page accesses. */
//# define DEBUG_GMR_ACCESS
#endif

#define VMSVGA_FIFO_EXTCMD_NONE                         0
#define VMSVGA_FIFO_EXTCMD_TERMINATE                    1
#define VMSVGA_FIFO_EXTCMD_SAVESTATE                    2
#define VMSVGA_FIFO_EXTCMD_LOADSTATE                    3
#define VMSVGA_FIFO_EXTCMD_RESET                        4
#define VMSVGA_FIFO_EXTCMD_UPDATE_SURFACE_HEAP_BUFFERS  5
#define VMSVGA_FIFO_EXTCMD_POWEROFF                     6

/** Size of the region to backup when switching into svga mode. */
#define VMSVGA_VGA_FB_BACKUP_SIZE                       _512K

/** @def VMSVGA_WITH_VGA_FB_BACKUP
 * Enables correct VGA MMIO read/write handling when VMSVGA is enabled.  It
 * is SLOW and probably not entirely right, but it helps with getting 3dmark
 * output and other stuff. */
#define VMSVGA_WITH_VGA_FB_BACKUP                       1

/** @def VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RING3
 * defined(VMSVGA_WITH_VGA_FB_BACKUP) && defined(IN_RING3)  */
#if (defined(VMSVGA_WITH_VGA_FB_BACKUP) && defined(IN_RING3)) || defined(DOXYGEN_RUNNING)
# define VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RING3         1
#else
# undef  VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RING3
#endif

/** @def VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RZ
 * defined(VMSVGA_WITH_VGA_FB_BACKUP) && !defined(IN_RING3)  */
#if (defined(VMSVGA_WITH_VGA_FB_BACKUP) && !defined(IN_RING3)) || defined(DOXYGEN_RUNNING)
# define VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RZ            1
#else
# undef  VMSVGA_WITH_VGA_FB_BACKUP_AND_IN_RZ
#endif


typedef struct
{
    PSSMHANDLE      pSSM;
    uint32_t        uVersion;
    uint32_t        uPass;
} VMSVGA_STATE_LOAD;
typedef VMSVGA_STATE_LOAD *PVMSVGA_STATE_LOAD;

/** Host screen viewport.
 * (4th quadrant with negated Y values - usual Windows and X11 world view.) */
typedef struct VMSVGAVIEWPORT
{
    uint32_t        x;                  /**< x coordinate (left). */
    uint32_t        y;                  /**< y coordinate (top). */
    uint32_t        cx;                 /**< width. */
    uint32_t        cy;                 /**< height. */
    /** Right side coordinate (exclusive). Same as x + cx. */
    uint32_t        xRight;
    /** First quadrant low y coordinate.
     * Same as y + cy - 1 in window coordinates. */
    uint32_t        yLowWC;
    /** First quadrant high y coordinate (exclusive) - yLowWC + cy.
     * Same as y - 1 in window coordinates. */
    uint32_t        yHighWC;
    /** Alignment padding. */
    uint32_t        uAlignment;
} VMSVGAVIEWPORT;

#ifdef VBOX_WITH_VMSVGA3D
typedef struct VMSVGAHWSCREEN *PVMSVGAHWSCREEN;
#endif

/**
 * Screen object state.
 */
typedef struct VMSVGASCREENOBJECT
{
    /** SVGA_SCREEN_* flags. */
    uint32_t    fuScreen;
    /** The screen object id. */
    uint32_t    idScreen;
    /** The screen dimensions. */
    int32_t     xOrigin;
    int32_t     yOrigin;
    uint32_t    cWidth;
    uint32_t    cHeight;
    /** Offset of the screen buffer in the guest VRAM. */
    uint32_t    offVRAM;
    /** Scanline pitch. */
    uint32_t    cbPitch;
    /** Bits per pixel. */
    uint32_t    cBpp;
    /** The physical DPI that the guest expects for this screen. Zero, if the guest is not DPI aware. */
    uint32_t    cDpi;
    bool        fDefined;
    bool        fModified;
    void       *pvScreenBitmap;
#ifdef VBOX_WITH_VMSVGA3D
    /** Pointer to the HW accelerated (3D) screen data. */
    R3PTRTYPE(PVMSVGAHWSCREEN) pHwScreen;
#endif
} VMSVGASCREENOBJECT;

/** Pointer to the private VMSVGA ring-3 state structure.
 * @todo Still not entirely satisfired with the type name, but better than
 *       the previous lower/upper case only distinction. */
typedef struct VMSVGAR3STATE *PVMSVGAR3STATE;
/** Pointer to the private (implementation specific) VMSVGA3d state. */
typedef struct VMSVGA3DSTATE *PVMSVGA3DSTATE;


/**
 * The VMSVGA device state.
 *
 * This instantatiated as VGASTATE::svga.
 */
typedef struct VMSVGAState
{
    /** Guest physical address of the FIFO memory range. */
    RTGCPHYS                    GCPhysFIFO;
    /** Size in bytes of the FIFO memory range.
     * This may be smaller than cbFIFOConfig after restoring an old VM state.  */
    uint32_t                    cbFIFO;
    /** The configured FIFO size. */
    uint32_t                    cbFIFOConfig;
    /** SVGA id. */
    uint32_t                    u32SVGAId;
    /** SVGA extensions enabled or not. */
    uint32_t                    fEnabled;
    /** SVGA memory area configured status. */
    uint32_t                    fConfigured;
    /** Device is busy handling FIFO requests (VMSVGA_BUSY_F_FIFO,
     *  VMSVGA_BUSY_F_EMT_FORCE). */
    uint32_t volatile           fBusy;
#define VMSVGA_BUSY_F_FIFO          RT_BIT_32(0) /**< The normal true/false busy FIFO bit. */
#define VMSVGA_BUSY_F_EMT_FORCE     RT_BIT_32(1) /**< Bit preventing race status flickering when EMT kicks the FIFO thread. */
    /** Traces (dirty page detection) enabled or not. */
    uint32_t                    fTraces;
    /** Guest OS identifier. */
    uint32_t                    u32GuestId;
    /** Scratch region size (VMSVGAState::au32ScratchRegion). */
    uint32_t                    cScratchRegion;
    /** Irq status. */
    uint32_t                    u32IrqStatus;
    /** Irq mask. */
    uint32_t                    u32IrqMask;
    /** Pitch lock. */
    uint32_t                    u32PitchLock;
    /** Current GMR id. (SVGA_REG_GMR_ID) */
    uint32_t                    u32CurrentGMRId;
    /** SVGA device capabilities. */
    uint32_t                    u32DeviceCaps;
    uint32_t                    u32DeviceCaps2; /* Used to be I/O port base address and Padding0. */
    /** Guest driver information (SVGA_REG_GUEST_DRIVER_*). */
    uint32_t                    u32GuestDriverId;
    uint32_t                    u32GuestDriverVer1;
    uint32_t                    u32GuestDriverVer2;
    uint32_t                    u32GuestDriverVer3;
    /** The last fence received. */
    uint32_t                    u32FenceLast;
    /** Port io index register. */
    uint32_t                    u32IndexReg;
    /** FIFO request semaphore. */
    SUPSEMEVENT                 hFIFORequestSem;
    /** The last seen SVGA_FIFO_CURSOR_COUNT value.
     * Used by the FIFO thread and its watchdog. */
    uint32_t                    uLastCursorUpdateCount;
    /** Indicates that the FIFO thread is sleeping and might need waking up. */
    bool volatile               fFIFOThreadSleeping;
    /** The legacy GFB mode registers. If used, they correspond to screen 0. */
    /** True when the guest modifies the GFB mode registers. */
    bool                        fGFBRegisters;
    /** SVGA 3D overlay enabled or not. */
    bool                        f3DOverlayEnabled;
    /** Indicates that the guest behaves incorrectly. */
    bool volatile               fBadGuest;
    /** Whether the VirtualBox extensions for VMSVGA device (new commands and caps) are enabled. */
    bool                        fVBoxExtensions;
    /** Whether MSAA support is enabled. */
    bool                        fVMSVGA3dMSAA;
    bool                        fVMSVGA2dGBO;
    bool                        afPadding[1];
    uint32_t                    uWidth;
    uint32_t                    uHeight;
    uint32_t                    uBpp;
    uint32_t                    cbScanline;
    uint32_t                    uHostBpp;
    /** Maximum width supported. */
    uint32_t                    u32MaxWidth;
    /** Maximum height supported. */
    uint32_t                    u32MaxHeight;
    /** Viewport rectangle, i.e. what's currently visible of the target host
     *  window.  This is usually (0,0)(uWidth,uHeight), but if the window is
     *  shrunk and scrolling applied, both the origin and size may differ.  */
    VMSVGAVIEWPORT              viewport;
    /** Action flags */
    uint32_t                    u32ActionFlags;
    /** SVGA 3d extensions enabled or not. */
    bool                        f3DEnabled;
    /** VRAM page monitoring enabled or not. */
    bool                        fVRAMTracking;
    /** External command to be executed in the FIFO thread. */
    uint8_t volatile            u8FIFOExtCommand;
    /** Set by vmsvgaR3RunExtCmdOnFifoThread when it temporarily resumes the FIFO
     * thread and does not want it do anything but the command. */
    bool volatile               fFifoExtCommandWakeup;
#ifdef DEBUG_GMR_ACCESS
    /** GMR debug access handler type handle. */
    PGMPHYSHANDLERTYPE          hGmrAccessHandlerType;
#endif
#if defined(VMSVGA_USE_FIFO_ACCESS_HANDLER) || defined(DEBUG_FIFO_ACCESS)
    /** FIFO debug access handler type handle. */
    PGMPHYSHANDLERTYPE          hFifoAccessHandlerType;
#endif
    /** Number of GMRs (VMSVGA_MAX_GMR_IDS, count of elements in VMSVGAR3STATE::paGMR array). */
    uint32_t                    cGMR;
    uint32_t                    uScreenOffset; /* Used only for loading older saved states. */

    /** Legacy cursor state. */
    uint32_t                    uCursorX;
    uint32_t                    uCursorY;
    uint32_t                    uCursorID;
    uint32_t                    uCursorOn;

    /** Scratch array.
     * Putting this at the end since it's big it probably not . */
    uint32_t                    au32ScratchRegion[VMSVGA_SCRATCH_SIZE];

    /** Array of SVGA3D_DEVCAP values, which are accessed via SVGA_REG_DEV_CAP. */
    uint32_t                    au32DevCaps[SVGA3D_DEVCAP_MAX];
    /** Index written to the SVGA_REG_DEV_CAP register. */
    uint32_t                    u32DevCapIndex;
    /** Low 32 bit of a command buffer address written to the SVGA_REG_COMMAND_LOW register. */
    uint32_t                    u32RegCommandLow;
    /** High 32 bit of a command buffer address written to the SVGA_REG_COMMAND_HIGH register. */
    uint32_t                    u32RegCommandHigh;

    STAMCOUNTER                 StatRegBitsPerPixelWr;
    STAMCOUNTER                 StatRegBusyWr;
    STAMCOUNTER                 StatRegCursorXWr;
    STAMCOUNTER                 StatRegCursorYWr;
    STAMCOUNTER                 StatRegCursorIdWr;
    STAMCOUNTER                 StatRegCursorOnWr;
    STAMCOUNTER                 StatRegDepthWr;
    STAMCOUNTER                 StatRegDisplayHeightWr;
    STAMCOUNTER                 StatRegDisplayIdWr;
    STAMCOUNTER                 StatRegDisplayIsPrimaryWr;
    STAMCOUNTER                 StatRegDisplayPositionXWr;
    STAMCOUNTER                 StatRegDisplayPositionYWr;
    STAMCOUNTER                 StatRegDisplayWidthWr;
    STAMCOUNTER                 StatRegEnableWr;
    STAMCOUNTER                 StatRegGmrIdWr;
    STAMCOUNTER                 StatRegGuestIdWr;
    STAMCOUNTER                 StatRegHeightWr;
    STAMCOUNTER                 StatRegIdWr;
    STAMCOUNTER                 StatRegIrqMaskWr;
    STAMCOUNTER                 StatRegNumDisplaysWr;
    STAMCOUNTER                 StatRegNumGuestDisplaysWr;
    STAMCOUNTER                 StatRegPaletteWr;
    STAMCOUNTER                 StatRegPitchLockWr;
    STAMCOUNTER                 StatRegPseudoColorWr;
    STAMCOUNTER                 StatRegReadOnlyWr;
    STAMCOUNTER                 StatRegScratchWr;
    STAMCOUNTER                 StatRegSyncWr;
    STAMCOUNTER                 StatRegTopWr;
    STAMCOUNTER                 StatRegTracesWr;
    STAMCOUNTER                 StatRegUnknownWr;
    STAMCOUNTER                 StatRegWidthWr;
    STAMCOUNTER                 StatRegCommandLowWr;
    STAMCOUNTER                 StatRegCommandHighWr;
    STAMCOUNTER                 StatRegDevCapWr;
    STAMCOUNTER                 StatRegCmdPrependLowWr;
    STAMCOUNTER                 StatRegCmdPrependHighWr;

    STAMCOUNTER                 StatRegBitsPerPixelRd;
    STAMCOUNTER                 StatRegBlueMaskRd;
    STAMCOUNTER                 StatRegBusyRd;
    STAMCOUNTER                 StatRegBytesPerLineRd;
    STAMCOUNTER                 StatRegCapabilitesRd;
    STAMCOUNTER                 StatRegConfigDoneRd;
    STAMCOUNTER                 StatRegCursorXRd;
    STAMCOUNTER                 StatRegCursorYRd;
    STAMCOUNTER                 StatRegCursorIdRd;
    STAMCOUNTER                 StatRegCursorOnRd;
    STAMCOUNTER                 StatRegDepthRd;
    STAMCOUNTER                 StatRegDisplayHeightRd;
    STAMCOUNTER                 StatRegDisplayIdRd;
    STAMCOUNTER                 StatRegDisplayIsPrimaryRd;
    STAMCOUNTER                 StatRegDisplayPositionXRd;
    STAMCOUNTER                 StatRegDisplayPositionYRd;
    STAMCOUNTER                 StatRegDisplayWidthRd;
    STAMCOUNTER                 StatRegEnableRd;
    STAMCOUNTER                 StatRegFbOffsetRd;
    STAMCOUNTER                 StatRegFbSizeRd;
    STAMCOUNTER                 StatRegFbStartRd;
    STAMCOUNTER                 StatRegGmrIdRd;
    STAMCOUNTER                 StatRegGmrMaxDescriptorLengthRd;
    STAMCOUNTER                 StatRegGmrMaxIdsRd;
    STAMCOUNTER                 StatRegGmrsMaxPagesRd;
    STAMCOUNTER                 StatRegGreenMaskRd;
    STAMCOUNTER                 StatRegGuestIdRd;
    STAMCOUNTER                 StatRegHeightRd;
    STAMCOUNTER                 StatRegHostBitsPerPixelRd;
    STAMCOUNTER                 StatRegIdRd;
    STAMCOUNTER                 StatRegIrqMaskRd;
    STAMCOUNTER                 StatRegMaxHeightRd;
    STAMCOUNTER                 StatRegMaxWidthRd;
    STAMCOUNTER                 StatRegMemorySizeRd;
    STAMCOUNTER                 StatRegMemRegsRd;
    STAMCOUNTER                 StatRegMemSizeRd;
    STAMCOUNTER                 StatRegMemStartRd;
    STAMCOUNTER                 StatRegNumDisplaysRd;
    STAMCOUNTER                 StatRegNumGuestDisplaysRd;
    STAMCOUNTER                 StatRegPaletteRd;
    STAMCOUNTER                 StatRegPitchLockRd;
    STAMCOUNTER                 StatRegPsuedoColorRd;
    STAMCOUNTER                 StatRegRedMaskRd;
    STAMCOUNTER                 StatRegScratchRd;
    STAMCOUNTER                 StatRegScratchSizeRd;
    STAMCOUNTER                 StatRegSyncRd;
    STAMCOUNTER                 StatRegTopRd;
    STAMCOUNTER                 StatRegTracesRd;
    STAMCOUNTER                 StatRegUnknownRd;
    STAMCOUNTER                 StatRegVramSizeRd;
    STAMCOUNTER                 StatRegWidthRd;
    STAMCOUNTER                 StatRegWriteOnlyRd;
    STAMCOUNTER                 StatRegCommandLowRd;
    STAMCOUNTER                 StatRegCommandHighRd;
    STAMCOUNTER                 StatRegMaxPrimBBMemRd;
    STAMCOUNTER                 StatRegGBMemSizeRd;
    STAMCOUNTER                 StatRegDevCapRd;
    STAMCOUNTER                 StatRegCmdPrependLowRd;
    STAMCOUNTER                 StatRegCmdPrependHighRd;
    STAMCOUNTER                 StatRegScrnTgtMaxWidthRd;
    STAMCOUNTER                 StatRegScrnTgtMaxHeightRd;
    STAMCOUNTER                 StatRegMobMaxSizeRd;
} VMSVGAState, VMSVGASTATE;


/**
 * The VMSVGA device state for ring-3
 *
 * This instantatiated as VGASTATER3::svga.
 */
typedef struct VMSVGASTATER3
{
    /** The R3 FIFO pointer. */
    R3PTRTYPE(uint32_t *)       pau32FIFO;
    /** R3 Opaque pointer to svga state. */
    R3PTRTYPE(PVMSVGAR3STATE)   pSvgaR3State;
    /** R3 Opaque pointer to 3d state. */
    R3PTRTYPE(PVMSVGA3DSTATE)   p3dState;
    /** The separate VGA frame buffer in svga mode.
     * Unlike the the boch-based VGA device implementation, VMSVGA seems to have a
     * separate frame buffer for VGA and allows concurrent use of both.  The SVGA
     * SDK is making use of this to do VGA text output while testing other things in
     * SVGA mode, displaying the result by switching back to VGA text mode.  So,
     * when entering SVGA mode we copy the first part of the frame buffer here and
     * direct VGA accesses here instead.  It is copied back when leaving SVGA mode. */
    R3PTRTYPE(uint8_t *)        pbVgaFrameBufferR3;
    /** R3 Opaque pointer to an external fifo cmd parameter. */
    R3PTRTYPE(void * volatile)  pvFIFOExtCmdParam;

    /** FIFO external command semaphore. */
    R3PTRTYPE(RTSEMEVENT)       hFIFOExtCmdSem;
    /** FIFO IO Thread. */
    R3PTRTYPE(PPDMTHREAD)       pFIFOIOThread;
} VMSVGASTATER3;


/**
 * The VMSVGA device state for ring-0
 *
 * This instantatiated as VGASTATER0::svga.
 */
typedef struct VMSVGASTATER0
{
    /** The R0 FIFO pointer.
     * @note This only points to the _first_ _page_ of the FIFO!  */
    R0PTRTYPE(uint32_t *)       pau32FIFO;
} VMSVGASTATER0;


typedef struct VGAState   *PVGASTATE;
typedef struct VGASTATER3 *PVGASTATER3;
typedef struct VGASTATER0 *PVGASTATER0;
typedef struct VGASTATERC *PVGASTATERC;
typedef CTX_SUFF(PVGASTATE) PVGASTATECC;

DECLCALLBACK(int) vmsvgaR3PciIORegionFifoMapUnmap(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                  RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType);
DECLCALLBACK(VBOXSTRICTRC) vmsvgaIORead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb);
DECLCALLBACK(VBOXSTRICTRC) vmsvgaIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb);
DECLCALLBACK(VBOXSTRICTRC) vmsvga3MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);
DECLCALLBACK(VBOXSTRICTRC) vmsvga3MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);

DECLCALLBACK(void) vmsvgaR3PortSetViewport(PPDMIDISPLAYPORT pInterface, uint32_t uScreenId,
                                         uint32_t x, uint32_t y, uint32_t cx, uint32_t cy);
DECLCALLBACK(void) vmsvgaR3PortReportMonitorPositions(PPDMIDISPLAYPORT pInterface, uint32_t cPositions, PCRTPOINT paPositions);

int vmsvgaR3Init(PPDMDEVINS pDevIns);
int vmsvgaR3Reset(PPDMDEVINS pDevIns);
int vmsvgaR3Destruct(PPDMDEVINS pDevIns);
int vmsvgaR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
int vmsvgaR3LoadDone(PPDMDEVINS pDevIns);
int vmsvgaR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM);
DECLCALLBACK(void) vmsvgaR3PowerOn(PPDMDEVINS pDevIns);
DECLCALLBACK(void) vmsvgaR3PowerOff(PPDMDEVINS pDevIns);
void vmsvgaR3FifoWatchdogTimer(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC);

#ifdef IN_RING3
VMSVGASCREENOBJECT *vmsvgaR3GetScreenObject(PVGASTATECC pThisCC, uint32_t idScreen);
int vmsvgaR3UpdateScreen(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen, int x, int y, int w, int h);
#endif

int vmsvgaR3GmrTransfer(PVGASTATE pThis, PVGASTATECC pThisCC, const SVGA3dTransferType enmTransferType,
                        uint8_t *pbHstBuf, uint32_t cbHstBuf, uint32_t offHst, int32_t cbHstPitch,
                        SVGAGuestPtr gstPtr, uint32_t offGst, int32_t cbGstPitch,
                        uint32_t cbWidth, uint32_t cHeight);

void vmsvgaR3ClipCopyBox(const SVGA3dSize *pSizeSrc, const SVGA3dSize *pSizeDest, SVGA3dCopyBox *pBox);
void vmsvgaR3ClipBox(const SVGA3dSize *pSize, SVGA3dBox *pBox);
void vmsvgaR3ClipRect(SVGASignedRect const *pBound, SVGASignedRect *pRect);
void vmsvgaR3Clip3dRect(SVGA3dRect const *pBound, SVGA3dRect RT_UNTRUSTED_GUEST *pRect);

/*
 * GBO (Guest Backed Object).
 * A GBO is a list of the guest pages. GBOs are used for VMSVGA MOBs (Memory OBjects)
 * and Object Tables which the guest shares with the host.
 *
 * A GBO is similar to a GMR. Nevertheless I'll create a new code for GBOs in order
 * to avoid tweaking and possibly breaking existing code. Moreover it will be probably possible to
 * map the guest pages into the host R3 memory and access them directly.
 */

#ifndef VMSVGA_WITH_PGM_LOCKING
/* GBO descriptor. */
typedef struct VMSVGAGBODESCRIPTOR
{
   RTGCPHYS                 GCPhys;
   uint64_t                 cPages;
} VMSVGAGBODESCRIPTOR, *PVMSVGAGBODESCRIPTOR;
typedef VMSVGAGBODESCRIPTOR const *PCVMSVGAGBODESCRIPTOR;
#endif

/* GBO.
 */
typedef struct VMSVGAGBO
{
    uint32_t                fGboFlags;
    uint32_t                cTotalPages;
    uint32_t                cbTotal;
#ifndef VMSVGA_WITH_PGM_LOCKING
    uint32_t                cDescriptors;
    PVMSVGAGBODESCRIPTOR    paDescriptors;
#else
    uint32_t                cSegsUsed;        /**< Number of segments used in VMSVGAGBO::paSegs. */
    void                   *pvDescriptors;    /**< Pointer to the memory for holding all the parallel arrays. */
    RTGCPHYS               *paGCPhysPages;    /**< Pointer to the array of guest physical address for the pages. */
    PPGMPAGEMAPLOCK         paPageLocks;      /**< Pointer to the array of PGM page map locks. */
    void                  **papvPages;        /**< Pointer to the host adresses of mapped pages. */
    PRTSGSEG                paSegs;           /**< Pointer to an array of segments. */
#endif
    void                   *pvHost; /* Pointer to cbTotal bytes on the host if VMSVGAGBO_F_HOST_BACKED is set. */
} VMSVGAGBO, *PVMSVGAGBO;
typedef VMSVGAGBO const *PCVMSVGAGBO;

#define VMSVGAGBO_F_OBSOLETE_0x1    0x1
#define VMSVGAGBO_F_HOST_BACKED     0x2

#ifndef VMSVGA_WITH_PGM_LOCKING
# define VMSVGA_IS_GBO_CREATED(a_Gbo) ((a_Gbo)->paDescriptors != NULL)
#else
# define VMSVGA_IS_GBO_CREATED(a_Gbo) ((a_Gbo)->pvDescriptors != NULL)
int vmsvgaR3GboAllocDescriptors(PVMSVGAGBO pGbo);
void vmsvgaR3GboFreeDescriptors(PVMSVGAGBO pGbo);
int vmsvgaR3GboMapPages(PPDMDEVINS pDevIns, PVMSVGAGBO pGbo);
#endif

int vmsvgaR3OTableReadSurface(PVMSVGAR3STATE pSvgaR3State, uint32_t sid, SVGAOTableSurfaceEntry *pEntrySurface);

/* MOB is also a GBO.
 */
typedef struct VMSVGAMOB
{
    AVLU32NODECORE          Core; /* Key is the mobid. */
    RTLISTNODE              nodeLRU;
    VMSVGAGBO               Gbo;
} VMSVGAMOB, *PVMSVGAMOB;
typedef VMSVGAMOB const *PCVMSVGAMOB;

PVMSVGAMOB vmsvgaR3MobGet(PVMSVGAR3STATE pSvgaR3State, SVGAMobId RT_UNTRUSTED_GUEST mobid);
int vmsvgaR3MobWrite(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob, uint32_t off, void const *pvData, uint32_t cbData);
int vmsvgaR3MobRead(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob, uint32_t off, void *pvData, uint32_t cbData);
int vmsvgaR3MobBackingStoreCreate(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob, uint32_t cbValid);
void vmsvgaR3MobBackingStoreDelete(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob);
int vmsvgaR3MobBackingStoreWriteToGuest(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob);
int vmsvgaR3MobBackingStoreReadFromGuest(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob);
void *vmsvgaR3MobBackingStorePtr(PVMSVGAMOB pMob, uint32_t off);

DECLINLINE(uint32_t) vmsvgaR3MobSize(PVMSVGAMOB pMob)
{
    if (pMob)
        return pMob->Gbo.cbTotal;
    return 0;
}

DECLINLINE(uint32_t) vmsvgaR3MobId(PVMSVGAMOB pMob)
{
    if (pMob)
        return pMob->Core.Key;
    return SVGA_ID_INVALID;
}

#ifdef DEBUG_sunlover
#define DEBUG_BREAKPOINT_TEST() do { ASMBreakpoint(); } while (0)
#else
#define DEBUG_BREAKPOINT_TEST() do { } while (0)
#endif

#endif /* !VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA_h */
