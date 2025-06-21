/* $Id$ */
/** @file
 * DevGeForce3 - NVIDIA GeForce3 Ti 500 device, internal header.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_DevGeForce3_h
#define VBOX_INCLUDED_SRC_Graphics_DevGeForce3_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdmdev.h>
#include <VBox/param.h>
#include <iprt/list.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** NVIDIA vendor ID */
#define GEFORCE3_VENDOR_ID          0x10DE
/** GeForce3 Ti 500 device ID */
#define GEFORCE3_DEVICE_ID          0x0201

/** Default VRAM size (64MB) */
#define GEFORCE3_VRAM_SIZE_DEFAULT  (64 * _1M)
/** Minimum VRAM size (16MB) */
#define GEFORCE3_VRAM_SIZE_MIN      (16 * _1M)
/** Maximum VRAM size (128MB) */
#define GEFORCE3_VRAM_SIZE_MAX      (128 * _1M)

/** Register space size (64KB) */
#define GEFORCE3_REGISTERS_SIZE     _64K

/** FIFO size (4KB) */
#define GEFORCE3_FIFO_SIZE          _4K

/** Register offsets */
#define GEFORCE3_REG_INTR           0x0100  /**< Interrupt status/control */
#define GEFORCE3_REG_INTR_EN        0x0140  /**< Interrupt enable */
#define GEFORCE3_REG_CONFIG         0x0200  /**< Configuration register */
#define GEFORCE3_REG_FIFO_PTR       0x0300  /**< FIFO pointer */
#define GEFORCE3_REG_FIFO_SIZE      0x0304  /**< FIFO size */
#define GEFORCE3_REG_DISPLAY_WIDTH  0x0400  /**< Display width */
#define GEFORCE3_REG_DISPLAY_HEIGHT 0x0404  /**< Display height */
#define GEFORCE3_REG_DISPLAY_BPP    0x0408  /**< Bits per pixel */
#define GEFORCE3_REG_DISPLAY_PITCH  0x040C  /**< Display pitch */
#define GEFORCE3_REG_DISPLAY_START  0x0410  /**< Display start address */

/** PCI BAR definitions */
#define GEFORCE3_PCI_BAR_REGISTERS  0  /**< BAR0: Memory-mapped registers */
#define GEFORCE3_PCI_BAR_FRAMEBUFFER 1  /**< BAR1: Frame buffer memory */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * GeForce3 command FIFO entry.
 */
typedef struct GEFORCE3FIFOCMD
{
    uint32_t    uCommand;       /**< Command type */
    uint32_t    uParam1;        /**< Parameter 1 */
    uint32_t    uParam2;        /**< Parameter 2 */
    uint32_t    uParam3;        /**< Parameter 3 */
} GEFORCE3FIFOCMD;
/** Pointer to a FIFO command. */
typedef GEFORCE3FIFOCMD *PGEFORCE3FIFOCMD;

/**
 * GeForce3 display mode.
 */
typedef struct GEFORCE3DISPLAYMODE
{
    uint32_t    uWidth;         /**< Display width */
    uint32_t    uHeight;        /**< Display height */
    uint32_t    uBpp;           /**< Bits per pixel */
    uint32_t    uPitch;         /**< Display pitch */
    uint32_t    uStartAddr;     /**< Start address in VRAM */
} GEFORCE3DISPLAYMODE;
/** Pointer to display mode. */
typedef GEFORCE3DISPLAYMODE *PGEFORCE3DISPLAYMODE;

/**
 * GeForce3 device state shared between all contexts.
 */
typedef struct GEFORCE3STATE
{
    /** The MMIO handle for registers. */
    IOMMMIOHANDLE           hMmioRegisters;
    /** The MMIO2 handle for VRAM. */
    PGMMMIO2HANDLE          hMmio2Vram;

    /** VRAM size in bytes. */
    uint32_t                cbVram;
    /** Register values. */
    uint32_t                aRegisters[GEFORCE3_REGISTERS_SIZE / sizeof(uint32_t)];

    /** Command FIFO. */
    GEFORCE3FIFOCMD         aFifo[GEFORCE3_FIFO_SIZE / sizeof(GEFORCE3FIFOCMD)];
    /** FIFO read pointer. */
    uint32_t                uFifoRead;
    /** FIFO write pointer. */
    uint32_t                uFifoWrite;

    /** Current display mode. */
    GEFORCE3DISPLAYMODE     DisplayMode;

    /** Device instance for the PDM lock. */
    PPDMDEVINS              pDevIns;
    /** The critical section protecting the device state. */
    PDMCRITSECT             CritSect;

    /** Flag indicating if the device is enabled. */
    bool                    fEnabled;
    /** Flag indicating if interrupts are enabled. */
    bool                    fInterruptsEnabled;

    /** The PCI device - must be last due to flexible array member. */
    PDMPCIDEV               PciDev;

} GEFORCE3STATE;
/** Pointer to the GeForce3 device state. */
typedef GEFORCE3STATE *PGEFORCE3STATE;

/**
 * GeForce3 device state for ring-3 context.
 */
typedef struct GEFORCE3STATER3
{
    /** Pointer to VRAM (ring-3). */
    uint8_t                *pbVram;

    /** Pointer to device instance. */
    PPDMDEVINS              pDevIns;

    /** Display connector interface. */
    PDMIBASE                IBase;
    /** Display connector interface. */
    PDMIDISPLAYCONNECTOR    IConnector;
    /** Pointer to display driver below us. */
    PPDMIDISPLAYCONNECTOR   pDrv;

} GEFORCE3STATER3;
/** Pointer to the GeForce3 device state for ring-3. */
typedef GEFORCE3STATER3 *PGEFORCE3STATER3;

/**
 * GeForce3 device state for ring-0 context.
 */
typedef struct GEFORCE3STATER0
{
    /** Dummy for now - extend as needed. */
    uint8_t                 uUnused;
} GEFORCE3STATER0;
/** Pointer to the GeForce3 device state for ring-0. */
typedef GEFORCE3STATER0 *PGEFORCE3STATER0;

/** The GeForce3 device state for the current context. */
typedef CTX_SUFF(GEFORCE3STATE) GEFORCE3STATECC;
/** Pointer to the GeForce3 device state for the current context. */
typedef CTX_SUFF(PGEFORCE3STATE) PGEFORCE3STATECC;


/*********************************************************************************************************************************
*   Function Prototypes                                                                                                          *
*********************************************************************************************************************************/

#ifdef IN_RING3
static DECLCALLBACK(int) geforce3R3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg);
static DECLCALLBACK(int) geforce3R3Destruct(PPDMDEVINS pDevIns);
static DECLCALLBACK(void) geforce3R3Reset(PPDMDEVINS pDevIns);
#endif

#if defined(IN_RING0) || defined(IN_RC)
static DECLCALLBACK(int) geforce3RZConstruct(PPDMDEVINS pDevIns);
#endif

#endif /* !VBOX_INCLUDED_SRC_Graphics_DevGeForce3_h */