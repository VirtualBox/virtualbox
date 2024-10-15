/* $Id: DevHda.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Intel HD Audio Controller Emulation - Structures.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Audio_DevHda_h
#define VBOX_INCLUDED_SRC_Audio_DevHda_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/path.h>
#include <VBox/vmm/pdmdev.h>
#include "AudioMixer.h"

/*
 * Compile time feature configuration.
 */

/** @def VBOX_HDA_WITH_ON_REG_ACCESS_DMA
 * Enables doing DMA work on certain register accesses (LPIB, WALCLK) in
 * addition to the DMA timer.   All but the last frame can be done during
 * register accesses (as we don't wish to leave the DMA timer w/o work to
 * do in case that upsets it). */
#if defined(DOXYGEN_RUNNING) || 0
# define VBOX_HDA_WITH_ON_REG_ACCESS_DMA
#endif

#ifdef DEBUG_andy
/** Enables strict mode, which checks for stuff which isn't supposed to happen.
 *  Be prepared for assertions coming in! */
//# define HDA_STRICT
#endif

/** @def HDA_AS_PCI_EXPRESS
 * Enables PCI express hardware.  */
#if defined(DOXYGEN_RUNNING) || 0
# define HDA_AS_PCI_EXPRESS
#endif

/** @def HDA_DEBUG_SILENCE
 * To debug silence coming from the guest in form of audio gaps.
 * Very crude implementation for now.
 * @todo probably borked atm */
#if defined(DOXYGEN_RUNNING) || 0
# define HDA_DEBUG_SILENCE
#endif


/*
 * Common pointer types.
 */
/** Pointer to an HDA stream (SDI / SDO).  */
typedef struct HDASTREAMR3 *PHDASTREAMR3;
/** Pointer to a shared HDA device state.  */
typedef struct HDASTATE    *PHDASTATE;
/** Pointer to a ring-3 HDA device state.  */
typedef struct HDASTATER3  *PHDASTATER3;
/** Pointer to an HDA mixer sink definition (ring-3). */
typedef struct HDAMIXERSINK *PHDAMIXERSINK;


/*
 * The rest of the headers.
 */
#include "DevHdaStream.h"
#include "DevHdaCodec.h"



/** @name Stream counts.
 *
 * At the moment we support 4 input + 4 output streams max, which is 8 in total.
 * Bidirectional streams are currently *not* supported.
 *
 * @note When changing any of those values, be prepared for some saved state
 *       fixups / trouble!
 * @{
 */
#define HDA_MAX_SDI                 4
#define HDA_MAX_SDO                 4
#define HDA_MAX_STREAMS             (HDA_MAX_SDI + HDA_MAX_SDO)
/** @} */
AssertCompile(HDA_MAX_SDI <= HDA_MAX_SDO);


/** @defgroup grp_hda_regs  HDA Register Definitions
 *
 * There are two variants for most register defines:
 *      - HDA_REG_XXX: Index into g_aHdaRegMap
 *      - HDA_RMX_XXX: Index into HDASTATE::au32Regs
 *
 * Use the HDA_REG and HDA_STREAM_REG macros to access registers where possible.
 *
 * @note The au32Regs[] layout is kept unchanged for saved state compatibility,
 *       thus the HDA_RMX_XXX assignments are for all purposes set in stone.
 *
 * @{ */

/** Number of general registers. */
#define HDA_NUM_GENERAL_REGS        36
/** Number of stream registers (10 registers per stream). */
#define HDA_NUM_STREAM_REGS         (HDA_MAX_STREAMS * 10 /* Each stream descriptor has 10 registers */)
/** Number of register after the stream registers. */
#define HDA_NUM_POST_STREAM_REGS    (2 + HDA_MAX_STREAMS * 2)
/** Number of total registers in the HDA's register map. */
#define HDA_NUM_REGS                (HDA_NUM_GENERAL_REGS + HDA_NUM_STREAM_REGS + HDA_NUM_POST_STREAM_REGS)
/** Total number of stream tags (channels). Index 0 is reserved / invalid. */
#define HDA_MAX_TAGS                16


/** Offset of the SD0 register map. */
#define HDA_REG_DESC_SD0_BASE       0x80

/* Registers */
#define HDA_REG_IND_NAME(x)         HDA_REG_##x
#define HDA_MEM_IND_NAME(x)         HDA_RMX_##x

/** Direct register access by HDASTATE::au32Reg index. */
#define HDA_REG_BY_IDX(a_pThis, a_idxReg)   ((a_pThis)->au32Regs[(a_idxReg)])

/** Accesses register @a ShortRegNm. */
#if defined(VBOX_STRICT) && defined(VBOX_HDA_CAN_ACCESS_REG_MAP)
# define HDA_REG(a_pThis, a_ShortRegNm)     (*hdaStrictRegAccessor(a_pThis, HDA_REG_IND_NAME(a_ShortRegNm), HDA_MEM_IND_NAME(a_ShortRegNm)))
#else
# define HDA_REG(a_pThis, a_ShortRegNm)     HDA_REG_BY_IDX(a_pThis, HDA_MEM_IND_NAME(a_ShortRegNm))
#endif

/** Indirect register access via g_aHdaRegMap[].idxReg. */
#define HDA_REG_IND(a_pThis, a_idxMap)      HDA_REG_BY_IDX(a_pThis, g_aHdaRegMap[(a_idxMap)].idxReg)


#define HDA_REG_GCAP                0           /* Range 0x00 - 0x01 */
#define HDA_RMX_GCAP                0
/**
 * GCAP HDASpec 3.3.2 This macro encodes the following information about HDA in a compact manner:
 *
 * oss (15:12) - Number of output streams supported.
 * iss (11:8)  - Number of input streams supported.
 * bss (7:3)   - Number of bidirectional streams supported.
 * bds (2:1)   - Number of serial data out (SDO) signals supported.
 * b64sup (0)  - 64 bit addressing supported.
 */
#define HDA_MAKE_GCAP(oss, iss, bss, bds, b64sup) \
    (  (((oss)   & 0xF)  << 12) \
     | (((iss)   & 0xF)  << 8)  \
     | (((bss)   & 0x1F) << 3)  \
     | (((bds)   & 0x3)  << 2)  \
     | ((b64sup) & 1))

#define HDA_REG_VMIN                1           /* 0x02 */
#define HDA_RMX_VMIN                1

#define HDA_REG_VMAJ                2           /* 0x03 */
#define HDA_RMX_VMAJ                2

#define HDA_REG_OUTPAY              3           /* 0x04-0x05 */
#define HDA_RMX_OUTPAY              3

#define HDA_REG_INPAY               4           /* 0x06-0x07 */
#define HDA_RMX_INPAY               4

#define HDA_REG_GCTL                5           /* 0x08-0x0B */
#define HDA_RMX_GCTL                5
#define HDA_GCTL_UNSOL              RT_BIT(8)   /* Accept Unsolicited Response Enable */
#define HDA_GCTL_FCNTRL             RT_BIT(1)   /* Flush Control */
#define HDA_GCTL_CRST               RT_BIT(0)   /* Controller Reset */

#define HDA_REG_WAKEEN              6           /* 0x0C */
#define HDA_RMX_WAKEEN              6

#define HDA_REG_STATESTS            7           /* 0x0E */
#define HDA_RMX_STATESTS            7
#define HDA_STATESTS_SCSF_MASK      0x7         /* State Change Status Flags (6.2.8). */

#define HDA_REG_GSTS                8           /* 0x10-0x11*/
#define HDA_RMX_GSTS                8
#define HDA_GSTS_FSTS               RT_BIT(1)   /* Flush Status */

#define HDA_REG_LLCH                9           /* 0x14 */
#define HDA_RMX_LLCH                114

#define HDA_REG_OUTSTRMPAY          10           /* 0x18 */
#define HDA_RMX_OUTSTRMPAY          112

#define HDA_REG_INSTRMPAY           11          /* 0x1a */
#define HDA_RMX_INSTRMPAY           113

#define HDA_REG_INTCTL              12          /* 0x20 */
#define HDA_RMX_INTCTL              9
#define HDA_INTCTL_GIE              RT_BIT(31)  /* Global Interrupt Enable */
#define HDA_INTCTL_CIE              RT_BIT(30)  /* Controller Interrupt Enable */
/** Bits 0-29 correspond to streams 0-29. */
#define HDA_STRMINT_MASK            0xFF        /* Streams 0-7 implemented. Applies to INTCTL and INTSTS. */

#define HDA_REG_INTSTS              13          /* 0x24 */
#define HDA_RMX_INTSTS              10
#define HDA_INTSTS_GIS              RT_BIT(31)  /* Global Interrupt Status */
#define HDA_INTSTS_CIS              RT_BIT(30)  /* Controller Interrupt Status */

#define HDA_REG_WALCLK              14          /* 0x30 */
/* NB: HDA_RMX_WALCLK is not defined because the register is not stored in memory. */

/**
 * @note The HDA specification defines a SSYNC register at offset 0x38.  The ICH6/ICH9
 *       datahseet defines SSYNC at offset 0x34.  The Linux HDA driver matches the datasheet.
 *       See also https://mailman.alsa-project.org/pipermail/alsa-devel/2011-March/037819.html
 */
#define HDA_REG_SSYNC               15          /* 0x34 */
#define HDA_RMX_SSYNC               12

#define HDA_REG_NEW_SSYNC           16          /* 0x38 */
#define HDA_RMX_NEW_SSYNC           HDA_RMX_SSYNC

#define HDA_REG_CORBLBASE           17          /* 0x40 */
#define HDA_RMX_CORBLBASE           13

#define HDA_REG_CORBUBASE           18          /* 0x44 */
#define HDA_RMX_CORBUBASE           14

#define HDA_REG_CORBWP              19          /* 0x48 */
#define HDA_RMX_CORBWP              15

#define HDA_REG_CORBRP              20          /* 0x4A */
#define HDA_RMX_CORBRP              16
#define HDA_CORBRP_RST              RT_BIT(15)  /* CORB Read Pointer Reset */

#define HDA_REG_CORBCTL             21          /* 0x4C */
#define HDA_RMX_CORBCTL             17
#define HDA_CORBCTL_DMA             RT_BIT(1)   /* Enable CORB DMA Engine */
#define HDA_CORBCTL_CMEIE           RT_BIT(0)   /* CORB Memory Error Interrupt Enable */

#define HDA_REG_CORBSTS             22          /* 0x4D */
#define HDA_RMX_CORBSTS             18

#define HDA_REG_CORBSIZE            23          /* 0x4E */
#define HDA_RMX_CORBSIZE            19
#define HDA_CORBSIZE_SZ_CAP         0xF0
#define HDA_CORBSIZE_SZ             0x3

/** Number of CORB buffer entries. */
#define HDA_CORB_SIZE               256
/** CORB element size (in bytes). */
#define HDA_CORB_ELEMENT_SIZE       4
/** Number of RIRB buffer entries. */
#define HDA_RIRB_SIZE               256
/** RIRB element size (in bytes). */
#define HDA_RIRB_ELEMENT_SIZE       8

#define HDA_REG_RIRBLBASE           24          /* 0x50 */
#define HDA_RMX_RIRBLBASE           20

#define HDA_REG_RIRBUBASE           25          /* 0x54 */
#define HDA_RMX_RIRBUBASE           21

#define HDA_REG_RIRBWP              26          /* 0x58 */
#define HDA_RMX_RIRBWP              22
#define HDA_RIRBWP_RST              RT_BIT(15)  /* RIRB Write Pointer Reset */

#define HDA_REG_RINTCNT             27          /* 0x5A */
#define HDA_RMX_RINTCNT             23

/** Maximum number of Response Interrupts. */
#define HDA_MAX_RINTCNT             256

#define HDA_REG_RIRBCTL             28          /* 0x5C */
#define HDA_RMX_RIRBCTL             24
#define HDA_RIRBCTL_ROIC            RT_BIT(2)   /* Response Overrun Interrupt Control */
#define HDA_RIRBCTL_RDMAEN          RT_BIT(1)   /* RIRB DMA Enable */
#define HDA_RIRBCTL_RINTCTL         RT_BIT(0)   /* Response Interrupt Control */

#define HDA_REG_RIRBSTS             29          /* 0x5D */
#define HDA_RMX_RIRBSTS             25
#define HDA_RIRBSTS_RIRBOIS         RT_BIT(2)   /* Response Overrun Interrupt Status */
#define HDA_RIRBSTS_RINTFL          RT_BIT(0)   /* Response Interrupt Flag */

#define HDA_REG_RIRBSIZE            30          /* 0x5E */
#define HDA_RMX_RIRBSIZE            26

#define HDA_REG_IC                  31          /* 0x60 */
#define HDA_RMX_IC                  27

#define HDA_REG_IR                  32          /* 0x64 */
#define HDA_RMX_IR                  28

#define HDA_REG_IRS                 33          /* 0x68 */
#define HDA_RMX_IRS                 29
#define HDA_IRS_IRV                 RT_BIT(1)   /* Immediate Result Valid */
#define HDA_IRS_ICB                 RT_BIT(0)   /* Immediate Command Busy */

#define HDA_REG_DPLBASE             34          /* 0x70 */
#define HDA_RMX_DPLBASE             30

#define HDA_REG_DPUBASE             35          /* 0x74 */
#define HDA_RMX_DPUBASE             31

#define DPBASE_ADDR_MASK            (~(uint64_t)0x7f)

#define HDA_STREAM_REG_DEF(name, num)           (HDA_REG_SD##num##name)
#define HDA_STREAM_RMX_DEF(name, num)           (HDA_RMX_SD##num##name)
/** @note sdnum here _MUST_ be stream reg number [0,7]. */
#if defined(VBOX_STRICT) && defined(VBOX_HDA_CAN_ACCESS_REG_MAP)
# define HDA_STREAM_REG(pThis, name, sdnum)      (*hdaStrictStreamRegAccessor((pThis), HDA_REG_SD0##name, HDA_RMX_SD0##name, (sdnum)))
#else
# define HDA_STREAM_REG(pThis, name, sdnum)      (HDA_REG_BY_IDX((pThis), HDA_RMX_SD0##name + (sdnum) * 10))
#endif

#define HDA_SD_NUM_FROM_REG(pThis, func, reg)   ((reg - HDA_STREAM_REG_DEF(func, 0)) / 10)
#define HDA_SD_TO_REG(a_Name, uSD)              (HDA_STREAM_REG_DEF(a_Name, 0) + (uSD) * 10)

/** @todo Condense marcos! */

#define HDA_REG_SD0CTL              HDA_NUM_GENERAL_REGS /* 0x80; other streams offset by 0x20 */
#define HDA_RMX_SD0CTL              32
#define HDA_RMX_SD1CTL              (HDA_STREAM_RMX_DEF(CTL, 0) + 10)
#define HDA_RMX_SD2CTL              (HDA_STREAM_RMX_DEF(CTL, 0) + 20)
#define HDA_RMX_SD3CTL              (HDA_STREAM_RMX_DEF(CTL, 0) + 30)
#define HDA_RMX_SD4CTL              (HDA_STREAM_RMX_DEF(CTL, 0) + 40)
#define HDA_RMX_SD5CTL              (HDA_STREAM_RMX_DEF(CTL, 0) + 50)
#define HDA_RMX_SD6CTL              (HDA_STREAM_RMX_DEF(CTL, 0) + 60)
#define HDA_RMX_SD7CTL              (HDA_STREAM_RMX_DEF(CTL, 0) + 70)

#define HDA_SDCTL_NUM_MASK          0xF
#define HDA_SDCTL_NUM_SHIFT         20
#define HDA_SDCTL_DIR               RT_BIT(19)  /* Direction (Bidirectional streams only!) */
#define HDA_SDCTL_TP                RT_BIT(18)  /* Traffic Priority (PCI Express) */
#define HDA_SDCTL_STRIPE_MASK       0x3
#define HDA_SDCTL_STRIPE_SHIFT      16
#define HDA_SDCTL_DEIE              RT_BIT(4)   /* Descriptor Error Interrupt Enable */
#define HDA_SDCTL_FEIE              RT_BIT(3)   /* FIFO Error Interrupt Enable */
#define HDA_SDCTL_IOCE              RT_BIT(2)   /* Interrupt On Completion Enable */
#define HDA_SDCTL_RUN               RT_BIT(1)   /* Stream Run */
#define HDA_SDCTL_SRST              RT_BIT(0)   /* Stream Reset */

#define HDA_REG_SD0STS              (HDA_NUM_GENERAL_REGS + 1) /* 0x83; other streams offset by 0x20 */
#define HDA_RMX_SD0STS              33
#define HDA_RMX_SD1STS              (HDA_STREAM_RMX_DEF(STS, 0) + 10)
#define HDA_RMX_SD2STS              (HDA_STREAM_RMX_DEF(STS, 0) + 20)
#define HDA_RMX_SD3STS              (HDA_STREAM_RMX_DEF(STS, 0) + 30)
#define HDA_RMX_SD4STS              (HDA_STREAM_RMX_DEF(STS, 0) + 40)
#define HDA_RMX_SD5STS              (HDA_STREAM_RMX_DEF(STS, 0) + 50)
#define HDA_RMX_SD6STS              (HDA_STREAM_RMX_DEF(STS, 0) + 60)
#define HDA_RMX_SD7STS              (HDA_STREAM_RMX_DEF(STS, 0) + 70)

#define HDA_SDSTS_FIFORDY           RT_BIT(5)   /* FIFO Ready */
#define HDA_SDSTS_DESE              RT_BIT(4)   /* Descriptor Error */
#define HDA_SDSTS_FIFOE             RT_BIT(3)   /* FIFO Error */
#define HDA_SDSTS_BCIS              RT_BIT(2)   /* Buffer Completion Interrupt Status */

#define HDA_REG_SD0LPIB             (HDA_NUM_GENERAL_REGS + 2) /* 0x84; other streams offset by 0x20 */
#define HDA_REG_SD1LPIB             (HDA_STREAM_REG_DEF(LPIB, 0) + 10) /* 0xA4 */
#define HDA_REG_SD2LPIB             (HDA_STREAM_REG_DEF(LPIB, 0) + 20) /* 0xC4 */
#define HDA_REG_SD3LPIB             (HDA_STREAM_REG_DEF(LPIB, 0) + 30) /* 0xE4 */
#define HDA_REG_SD4LPIB             (HDA_STREAM_REG_DEF(LPIB, 0) + 40) /* 0x104 */
#define HDA_REG_SD5LPIB             (HDA_STREAM_REG_DEF(LPIB, 0) + 50) /* 0x124 */
#define HDA_REG_SD6LPIB             (HDA_STREAM_REG_DEF(LPIB, 0) + 60) /* 0x144 */
#define HDA_REG_SD7LPIB             (HDA_STREAM_REG_DEF(LPIB, 0) + 70) /* 0x164 */
#define HDA_RMX_SD0LPIB             34
#define HDA_RMX_SD1LPIB             (HDA_STREAM_RMX_DEF(LPIB, 0) + 10)
#define HDA_RMX_SD2LPIB             (HDA_STREAM_RMX_DEF(LPIB, 0) + 20)
#define HDA_RMX_SD3LPIB             (HDA_STREAM_RMX_DEF(LPIB, 0) + 30)
#define HDA_RMX_SD4LPIB             (HDA_STREAM_RMX_DEF(LPIB, 0) + 40)
#define HDA_RMX_SD5LPIB             (HDA_STREAM_RMX_DEF(LPIB, 0) + 50)
#define HDA_RMX_SD6LPIB             (HDA_STREAM_RMX_DEF(LPIB, 0) + 60)
#define HDA_RMX_SD7LPIB             (HDA_STREAM_RMX_DEF(LPIB, 0) + 70)

#define HDA_REG_SD0CBL              (HDA_NUM_GENERAL_REGS + 3) /* 0x88; other streams offset by 0x20 */
#define HDA_RMX_SD0CBL              35
#define HDA_RMX_SD1CBL              (HDA_STREAM_RMX_DEF(CBL, 0) + 10)
#define HDA_RMX_SD2CBL              (HDA_STREAM_RMX_DEF(CBL, 0) + 20)
#define HDA_RMX_SD3CBL              (HDA_STREAM_RMX_DEF(CBL, 0) + 30)
#define HDA_RMX_SD4CBL              (HDA_STREAM_RMX_DEF(CBL, 0) + 40)
#define HDA_RMX_SD5CBL              (HDA_STREAM_RMX_DEF(CBL, 0) + 50)
#define HDA_RMX_SD6CBL              (HDA_STREAM_RMX_DEF(CBL, 0) + 60)
#define HDA_RMX_SD7CBL              (HDA_STREAM_RMX_DEF(CBL, 0) + 70)

#define HDA_REG_SD0LVI              (HDA_NUM_GENERAL_REGS + 4) /* 0x8C; other streams offset by 0x20 */
#define HDA_RMX_SD0LVI              36
#define HDA_RMX_SD1LVI              (HDA_STREAM_RMX_DEF(LVI, 0) + 10)
#define HDA_RMX_SD2LVI              (HDA_STREAM_RMX_DEF(LVI, 0) + 20)
#define HDA_RMX_SD3LVI              (HDA_STREAM_RMX_DEF(LVI, 0) + 30)
#define HDA_RMX_SD4LVI              (HDA_STREAM_RMX_DEF(LVI, 0) + 40)
#define HDA_RMX_SD5LVI              (HDA_STREAM_RMX_DEF(LVI, 0) + 50)
#define HDA_RMX_SD6LVI              (HDA_STREAM_RMX_DEF(LVI, 0) + 60)
#define HDA_RMX_SD7LVI              (HDA_STREAM_RMX_DEF(LVI, 0) + 70)

#define HDA_REG_SD0FIFOW            (HDA_NUM_GENERAL_REGS + 5) /* 0x8E; other streams offset by 0x20 */
#define HDA_RMX_SD0FIFOW            37
#define HDA_RMX_SD1FIFOW            (HDA_STREAM_RMX_DEF(FIFOW, 0) + 10)
#define HDA_RMX_SD2FIFOW            (HDA_STREAM_RMX_DEF(FIFOW, 0) + 20)
#define HDA_RMX_SD3FIFOW            (HDA_STREAM_RMX_DEF(FIFOW, 0) + 30)
#define HDA_RMX_SD4FIFOW            (HDA_STREAM_RMX_DEF(FIFOW, 0) + 40)
#define HDA_RMX_SD5FIFOW            (HDA_STREAM_RMX_DEF(FIFOW, 0) + 50)
#define HDA_RMX_SD6FIFOW            (HDA_STREAM_RMX_DEF(FIFOW, 0) + 60)
#define HDA_RMX_SD7FIFOW            (HDA_STREAM_RMX_DEF(FIFOW, 0) + 70)

/*
 * ICH6 datasheet defined limits for FIFOW values (18.2.38).
 */
#define HDA_SDFIFOW_8B              0x2
#define HDA_SDFIFOW_16B             0x3
#define HDA_SDFIFOW_32B             0x4

#define HDA_REG_SD0FIFOS            (HDA_NUM_GENERAL_REGS + 6) /* 0x90; other streams offset by 0x20 */
#define HDA_RMX_SD0FIFOS            38
#define HDA_RMX_SD1FIFOS            (HDA_STREAM_RMX_DEF(FIFOS, 0) + 10)
#define HDA_RMX_SD2FIFOS            (HDA_STREAM_RMX_DEF(FIFOS, 0) + 20)
#define HDA_RMX_SD3FIFOS            (HDA_STREAM_RMX_DEF(FIFOS, 0) + 30)
#define HDA_RMX_SD4FIFOS            (HDA_STREAM_RMX_DEF(FIFOS, 0) + 40)
#define HDA_RMX_SD5FIFOS            (HDA_STREAM_RMX_DEF(FIFOS, 0) + 50)
#define HDA_RMX_SD6FIFOS            (HDA_STREAM_RMX_DEF(FIFOS, 0) + 60)
#define HDA_RMX_SD7FIFOS            (HDA_STREAM_RMX_DEF(FIFOS, 0) + 70)

/* The ICH6 datasheet defines limits for FIFOS registers (18.2.39).
   Formula: size - 1
   Other values not listed are not supported. */

#define HDA_SDIFIFO_120B            0x77        /* 8-, 16-, 20-, 24-, 32-bit Input Streams */
#define HDA_SDIFIFO_160B            0x9F        /* 20-, 24-bit Input Streams Streams */

#define HDA_SDOFIFO_16B             0x0F        /* 8-, 16-, 20-, 24-, 32-bit Output Streams */
#define HDA_SDOFIFO_32B             0x1F        /* 8-, 16-, 20-, 24-, 32-bit Output Streams */
#define HDA_SDOFIFO_64B             0x3F        /* 8-, 16-, 20-, 24-, 32-bit Output Streams */
#define HDA_SDOFIFO_128B            0x7F        /* 8-, 16-, 20-, 24-, 32-bit Output Streams */
#define HDA_SDOFIFO_192B            0xBF        /* 8-, 16-, 20-, 24-, 32-bit Output Streams */
#define HDA_SDOFIFO_256B            0xFF        /* 20-, 24-bit Output Streams */

#define HDA_REG_SD0FMT              (HDA_NUM_GENERAL_REGS + 7) /* 0x92; other streams offset by 0x20 */
#define HDA_RMX_SD0FMT              39
#define HDA_RMX_SD1FMT              (HDA_STREAM_RMX_DEF(FMT, 0) + 10)
#define HDA_RMX_SD2FMT              (HDA_STREAM_RMX_DEF(FMT, 0) + 20)
#define HDA_RMX_SD3FMT              (HDA_STREAM_RMX_DEF(FMT, 0) + 30)
#define HDA_RMX_SD4FMT              (HDA_STREAM_RMX_DEF(FMT, 0) + 40)
#define HDA_RMX_SD5FMT              (HDA_STREAM_RMX_DEF(FMT, 0) + 50)
#define HDA_RMX_SD6FMT              (HDA_STREAM_RMX_DEF(FMT, 0) + 60)
#define HDA_RMX_SD7FMT              (HDA_STREAM_RMX_DEF(FMT, 0) + 70)

#define HDA_REG_SD0BDPL             (HDA_NUM_GENERAL_REGS + 8) /* 0x98; other streams offset by 0x20 */
#define HDA_RMX_SD0BDPL             40
#define HDA_RMX_SD1BDPL             (HDA_STREAM_RMX_DEF(BDPL, 0) + 10)
#define HDA_RMX_SD2BDPL             (HDA_STREAM_RMX_DEF(BDPL, 0) + 20)
#define HDA_RMX_SD3BDPL             (HDA_STREAM_RMX_DEF(BDPL, 0) + 30)
#define HDA_RMX_SD4BDPL             (HDA_STREAM_RMX_DEF(BDPL, 0) + 40)
#define HDA_RMX_SD5BDPL             (HDA_STREAM_RMX_DEF(BDPL, 0) + 50)
#define HDA_RMX_SD6BDPL             (HDA_STREAM_RMX_DEF(BDPL, 0) + 60)
#define HDA_RMX_SD7BDPL             (HDA_STREAM_RMX_DEF(BDPL, 0) + 70)

#define HDA_REG_SD0BDPU             (HDA_NUM_GENERAL_REGS + 9) /* 0x9C; other streams offset by 0x20 */
#define HDA_RMX_SD0BDPU             41
#define HDA_RMX_SD1BDPU             (HDA_STREAM_RMX_DEF(BDPU, 0) + 10)
#define HDA_RMX_SD2BDPU             (HDA_STREAM_RMX_DEF(BDPU, 0) + 20)
#define HDA_RMX_SD3BDPU             (HDA_STREAM_RMX_DEF(BDPU, 0) + 30)
#define HDA_RMX_SD4BDPU             (HDA_STREAM_RMX_DEF(BDPU, 0) + 40)
#define HDA_RMX_SD5BDPU             (HDA_STREAM_RMX_DEF(BDPU, 0) + 50)
#define HDA_RMX_SD6BDPU             (HDA_STREAM_RMX_DEF(BDPU, 0) + 60)
#define HDA_RMX_SD7BDPU             (HDA_STREAM_RMX_DEF(BDPU, 0) + 70)

#define HDA_CODEC_CAD_SHIFT         28
/** Encodes the (required) LUN into a codec command. */
#define HDA_CODEC_CMD(cmd, lun)     ((cmd) | (lun << HDA_CODEC_CAD_SHIFT))

#define HDA_SDFMT_NON_PCM_SHIFT     15
#define HDA_SDFMT_NON_PCM_MASK      0x1
#define HDA_SDFMT_BASE_RATE_SHIFT   14
#define HDA_SDFMT_BASE_RATE_MASK    0x1
#define HDA_SDFMT_MULT_SHIFT        11
#define HDA_SDFMT_MULT_MASK         0x7
#define HDA_SDFMT_DIV_SHIFT         8
#define HDA_SDFMT_DIV_MASK          0x7
#define HDA_SDFMT_BITS_SHIFT        4
#define HDA_SDFMT_BITS_MASK         0x7
#define HDA_SDFMT_CHANNELS_MASK     0xF

#define HDA_SDFMT_TYPE              RT_BIT(15)
#define HDA_SDFMT_TYPE_PCM          (0)
#define HDA_SDFMT_TYPE_NON_PCM      (1)

#define HDA_SDFMT_BASE              RT_BIT(14)
#define HDA_SDFMT_BASE_48KHZ        (0)
#define HDA_SDFMT_BASE_44KHZ        (1)

#define HDA_SDFMT_MULT_1X           (0)
#define HDA_SDFMT_MULT_2X           (1)
#define HDA_SDFMT_MULT_3X           (2)
#define HDA_SDFMT_MULT_4X           (3)

#define HDA_SDFMT_DIV_1X            (0)
#define HDA_SDFMT_DIV_2X            (1)
#define HDA_SDFMT_DIV_3X            (2)
#define HDA_SDFMT_DIV_4X            (3)
#define HDA_SDFMT_DIV_5X            (4)
#define HDA_SDFMT_DIV_6X            (5)
#define HDA_SDFMT_DIV_7X            (6)
#define HDA_SDFMT_DIV_8X            (7)

#define HDA_SDFMT_8_BIT             (0)
#define HDA_SDFMT_16_BIT            (1)
#define HDA_SDFMT_20_BIT            (2)
#define HDA_SDFMT_24_BIT            (3)
#define HDA_SDFMT_32_BIT            (4)

#define HDA_SDFMT_CHAN_MONO         (0)
#define HDA_SDFMT_CHAN_STEREO       (1)

/** Emits a SDnFMT register format.
 * Also being used in the codec's converter format. */
#define HDA_SDFMT_MAKE(_afNonPCM, _aBaseRate, _aMult, _aDiv, _aBits, _aChan)    \
    (  (((_afNonPCM)  & HDA_SDFMT_NON_PCM_MASK)   << HDA_SDFMT_NON_PCM_SHIFT)   \
     | (((_aBaseRate) & HDA_SDFMT_BASE_RATE_MASK) << HDA_SDFMT_BASE_RATE_SHIFT) \
     | (((_aMult)     & HDA_SDFMT_MULT_MASK)      << HDA_SDFMT_MULT_SHIFT)      \
     | (((_aDiv)      & HDA_SDFMT_DIV_MASK)       << HDA_SDFMT_DIV_SHIFT)       \
     | (((_aBits)     & HDA_SDFMT_BITS_MASK)      << HDA_SDFMT_BITS_SHIFT)      \
     | ( (_aChan)     & HDA_SDFMT_CHANNELS_MASK))


/* Post stream registers: */
#define HDA_REG_MLCH                (HDA_NUM_GENERAL_REGS + HDA_NUM_STREAM_REGS) /* 0xc00 */
#define HDA_RMX_MLCH                115
#define HDA_REG_MLCD                (HDA_REG_MLCH + 1)      /* 0xc04 */
#define HDA_RMX_MLCD                116

/* Registers added/specific-to skylake/broxton: */
#define HDA_SD_NUM_FROM_SKYLAKE_REG(a_Name, a_iMap)   (((a_iMap) - HDA_STREAM_REG_DEF(a_Name, 0)) / 2)

#define HDA_REG_SD0DPIB             (HDA_REG_MLCD + 1)      /* 0x1084 */
#define HDA_REG_SD1DPIB             (HDA_REG_SD0DPIB + 1*2)
#define HDA_REG_SD2DPIB             (HDA_REG_SD0DPIB + 2*2)
#define HDA_REG_SD3DPIB             (HDA_REG_SD0DPIB + 3*2)
#define HDA_REG_SD4DPIB             (HDA_REG_SD0DPIB + 4*2)
#define HDA_REG_SD5DPIB             (HDA_REG_SD0DPIB + 5*2)
#define HDA_REG_SD6DPIB             (HDA_REG_SD0DPIB + 6*2)
#define HDA_REG_SD7DPIB             (HDA_REG_SD0DPIB + 7*2)

#define HDA_RMX_SD0DPIB             HDA_RMX_SD0LPIB
#define HDA_RMX_SD1DPIB             HDA_RMX_SD1LPIB
#define HDA_RMX_SD2DPIB             HDA_RMX_SD2LPIB
#define HDA_RMX_SD3DPIB             HDA_RMX_SD3LPIB
#define HDA_RMX_SD4DPIB             HDA_RMX_SD4LPIB
#define HDA_RMX_SD5DPIB             HDA_RMX_SD5LPIB
#define HDA_RMX_SD6DPIB             HDA_RMX_SD6LPIB
#define HDA_RMX_SD7DPIB             HDA_RMX_SD7LPIB

#define HDA_REG_SD0EFIFOS           (HDA_REG_SD0DPIB + 1)   /* 0x1094 */
#define HDA_REG_SD1EFIFOS           (HDA_REG_SD0EFIFOS + 1*2)
#define HDA_REG_SD2EFIFOS           (HDA_REG_SD0EFIFOS + 2*2)
#define HDA_REG_SD3EFIFOS           (HDA_REG_SD0EFIFOS + 3*2)
#define HDA_REG_SD4EFIFOS           (HDA_REG_SD0EFIFOS + 4*2)
#define HDA_REG_SD5EFIFOS           (HDA_REG_SD0EFIFOS + 5*2)
#define HDA_REG_SD6EFIFOS           (HDA_REG_SD0EFIFOS + 6*2)
#define HDA_REG_SD7EFIFOS           (HDA_REG_SD0EFIFOS + 7*2)

#define HDA_RMX_SD0EFIFOS           117
#define HDA_RMX_SD1EFIFOS           (HDA_RMX_SD0EFIFOS + 1)
#define HDA_RMX_SD2EFIFOS           (HDA_RMX_SD0EFIFOS + 2)
#define HDA_RMX_SD3EFIFOS           (HDA_RMX_SD0EFIFOS + 3)
#define HDA_RMX_SD4EFIFOS           (HDA_RMX_SD0EFIFOS + 4)
#define HDA_RMX_SD5EFIFOS           (HDA_RMX_SD0EFIFOS + 5)
#define HDA_RMX_SD6EFIFOS           (HDA_RMX_SD0EFIFOS + 6)
#define HDA_RMX_SD7EFIFOS           (HDA_RMX_SD0EFIFOS + 7)

/** @} */ /* grp_hda_regs */


/**
 * Buffer descriptor list entry (BDLE).
 *
 * See 3.6.3 in HDA specs rev 1.0a (2010-06-17).
 */
typedef struct HDABDLEDESC
{
    /** Starting address of the actual buffer. Must be 128-bit aligned. */
    uint64_t     u64BufAddr;
    /** Size of the actual buffer (in bytes). */
    uint32_t     u32BufSize;
    /** HDA_BDLE_F_XXX.
     *
     * Bit 0: IOC - Interrupt on completion / HDA_BDLE_F_IOC.
     * The controller will generate an interrupt when the last byte of the buffer
     * has been fetched by the DMA engine.
     *
     * Bits 31:1 are reserved for further use and must be 0. */
    uint32_t     fFlags;
} HDABDLEDESC, *PHDABDLEDESC;
AssertCompileSize(HDABDLEDESC, 16); /* Always 16 byte. Also must be aligned on 128-byte boundary. */

/** Interrupt on completion (IOC) flag. */
#define HDA_BDLE_F_IOC              RT_BIT(0)


/**
 * HDA mixer sink definition (ring-3).
 *
 * Its purpose is to know which audio mixer sink is bound to which SDn
 * (SDI/SDO) device stream.
 *
 * This is needed in order to handle interleaved streams (that is, multiple
 * channels in one stream) or non-interleaved streams (each channel has a
 * dedicated stream).
 *
 * This is only known to the actual device emulation level.
 */
typedef struct HDAMIXERSINK
{
    R3PTRTYPE(PHDASTREAM)   pStreamShared;
    R3PTRTYPE(PHDASTREAMR3) pStreamR3;
    /** Pointer to the actual audio mixer sink. */
    R3PTRTYPE(PAUDMIXSINK)  pMixSink;
} HDAMIXERSINK;

/**
 * Mapping a stream tag to an HDA stream (ring-3).
 */
typedef struct HDATAG
{
    /** Own stream tag. */
    uint8_t                 uTag;
    uint8_t                 Padding[7];
    /** Pointer to associated stream. */
    R3PTRTYPE(PHDASTREAMR3) pStreamR3;
} HDATAG;
/** Pointer to a HDA stream tag mapping. */
typedef HDATAG *PHDATAG;

/**
 * Shared ICH Intel HD audio controller state.
 */
typedef struct HDASTATE
{
    /** Critical section protecting the HDA state. */
    PDMCRITSECT             CritSect;
    /** Internal stream states (aligned on 64 byte boundrary). */
    HDASTREAM               aStreams[HDA_MAX_STREAMS];
    /** The HDA's register set. */
    uint32_t                au32Regs[HDA_NUM_REGS];
    /** CORB buffer base address. */
    uint64_t                u64CORBBase;
    /** RIRB buffer base address. */
    uint64_t                u64RIRBBase;
    /** DMA base address.
     *  Made out of DPLBASE + DPUBASE (3.3.32 + 3.3.33). */
    uint64_t                u64DPBase;
    /** Size in bytes of CORB buffer (#au32CorbBuf). */
    uint32_t                cbCorbBuf;
    /** Size in bytes of RIRB buffer (#au64RirbBuf). */
    uint32_t                cbRirbBuf;
    /** Response Interrupt Count (RINTCNT). */
    uint16_t                u16RespIntCnt;
    /** DMA position buffer enable bit. */
    bool                    fDMAPosition;
    /** Current IRQ level. */
    uint8_t                 u8IRQL;
    /** Config: Internal input DMA buffer size override, specified in milliseconds.
     * Zero means default size according to buffer and stream config.
     * @sa BufSizeInMs config value.  */
    uint16_t                cMsCircBufIn;
    /** Config: Internal output DMA buffer size override, specified in milliseconds.
     * Zero means default size according to buffer and stream config.
     * @sa BufSizeOutMs config value.  */
    uint16_t                cMsCircBufOut;
    /** The start time of the wall clock (WALCLK), measured on the virtual sync clock. */
    uint64_t                tsWalClkStart;
    /** CORB DMA task handle.
     * We use this when there is stuff we cannot handle in ring-0.  */
    PDMTASKHANDLE           hCorbDmaTask;
    /** The CORB buffer. */
    uint32_t                au32CorbBuf[HDA_CORB_SIZE];
    /** Pointer to RIRB buffer. */
    uint64_t                au64RirbBuf[HDA_RIRB_SIZE];

    /** PCI Region \#0: 16KB of MMIO stuff. */
    IOMMMIOHANDLE           hMmio;

#ifdef VBOX_HDA_WITH_ON_REG_ACCESS_DMA
    STAMCOUNTER             StatAccessDmaOutput;
    STAMCOUNTER             StatAccessDmaOutputToR3;
#endif
#ifdef VBOX_WITH_STATISTICS
    STAMPROFILE             StatIn;
    STAMPROFILE             StatOut;
    STAMCOUNTER             StatBytesRead;
    STAMCOUNTER             StatBytesWritten;

    /** @name Register statistics.
     * The array members run parallel to g_aHdaRegMap.
     * @{ */
    STAMCOUNTER             aStatRegReads[HDA_NUM_REGS];
    STAMCOUNTER             aStatRegReadsToR3[HDA_NUM_REGS];
    STAMCOUNTER             aStatRegWrites[HDA_NUM_REGS];
    STAMCOUNTER             aStatRegWritesToR3[HDA_NUM_REGS];
    STAMCOUNTER             StatRegMultiReadsRZ;
    STAMCOUNTER             StatRegMultiReadsR3;
    STAMCOUNTER             StatRegMultiWritesRZ;
    STAMCOUNTER             StatRegMultiWritesR3;
    STAMCOUNTER             StatRegSubWriteRZ;
    STAMCOUNTER             StatRegSubWriteR3;
    STAMCOUNTER             StatRegUnknownReads;
    STAMCOUNTER             StatRegUnknownWrites;
    STAMCOUNTER             StatRegWritesBlockedByReset;
    STAMCOUNTER             StatRegWritesBlockedByRun;
    /** @} */
#endif

#ifdef DEBUG
    /** Debug stuff.
     * @todo Make STAM values out some of this? */
    struct
    {
# if 0 /* unused */
        /** Timestamp (in ns) of the last timer callback (hdaTimer).
         * Used to calculate the time actually elapsed between two timer callbacks. */
        uint64_t                tsTimerLastCalledNs;
# endif
        /** IRQ debugging information. */
        struct
        {
            /** Timestamp (in ns) of last processed (asserted / deasserted) IRQ. */
            uint64_t            tsProcessedLastNs;
            /** Timestamp (in ns) of last asserted IRQ. */
            uint64_t            tsAssertedNs;
# if 0 /* unused */
            /** How many IRQs have been asserted already. */
            uint64_t            cAsserted;
            /** Accumulated elapsed time (in ns) of all IRQ being asserted. */
            uint64_t            tsAssertedTotalNs;
            /** Timestamp (in ns) of last deasserted IRQ. */
            uint64_t            tsDeassertedNs;
            /** How many IRQs have been deasserted already. */
            uint64_t            cDeasserted;
            /** Accumulated elapsed time (in ns) of all IRQ being deasserted. */
            uint64_t            tsDeassertedTotalNs;
# endif
        } IRQ;
    } Dbg;
#endif
    /** This is for checking that the build was correctly configured in all contexts.
     *  This is set to HDASTATE_ALIGNMENT_CHECK_MAGIC. */
    uint64_t                uAlignmentCheckMagic;
} HDASTATE;
AssertCompileMemberAlignment(HDASTATE, aStreams, 64);
/** Pointer to a shared HDA device state.  */
typedef HDASTATE *PHDASTATE;

/** Value for HDASTATE:uAlignmentCheckMagic. */
#define HDASTATE_ALIGNMENT_CHECK_MAGIC  UINT64_C(0x1298afb75893e059)

/**
 * Ring-0 ICH Intel HD audio controller state.
 */
typedef struct HDASTATER0
{
# if 0 /* Codec is not yet kosher enough for ring-0.  @bugref{9890c64} */
    /** Pointer to HDA codec to use. */
    HDACODECR0              Codec;
# else
    uint32_t                u32Dummy;
# endif
} HDASTATER0;
/** Pointer to a ring-0 HDA device state.  */
typedef HDASTATER0 *PHDASTATER0;

/**
 * Ring-3 ICH Intel HD audio controller state.
 */
typedef struct HDASTATER3
{
    /** Internal stream states. */
    HDASTREAMR3             aStreams[HDA_MAX_STREAMS];
    /** Mapping table between stream tags and stream states. */
    HDATAG                  aTags[HDA_MAX_TAGS];
    /** R3 Pointer to the device instance. */
    PPDMDEVINSR3            pDevIns;
    /** The base interface for LUN\#0. */
    PDMIBASE                IBase;
    /** List of associated LUN drivers (HDADRIVER). */
    RTLISTANCHORR3          lstDrv;
    /** The device' software mixer. */
    R3PTRTYPE(PAUDIOMIXER)  pMixer;
    /** HDA sink for (front) output. */
    HDAMIXERSINK            SinkFront;
#ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
    /** HDA sink for center / LFE output. */
    HDAMIXERSINK            SinkCenterLFE;
    /** HDA sink for rear output. */
    HDAMIXERSINK            SinkRear;
#endif
    /** HDA mixer sink for line input. */
    HDAMIXERSINK            SinkLineIn;
#ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
    /** Audio mixer sink for microphone input. */
    HDAMIXERSINK            SinkMicIn;
#endif
    /** Debug stuff. */
    struct
    {
        /** Whether debugging is enabled or not. */
        bool                    fEnabled;
        /** Path where to dump the debug output to.
         *  Can be NULL, in which the system's temporary directory will be used then. */
        R3PTRTYPE(char *)       pszOutPath;
    } Dbg;
    /** Align the codec state on a cache line. */
    uint64_t                au64Padding[3];
    /** The HDA codec state. */
    HDACODECR3              Codec;
} HDASTATER3;
AssertCompileMemberAlignment(HDASTATER3, Codec, 64);


/** Pointer to the context specific HDA state (HDASTATER3 or HDASTATER0). */
typedef CTX_SUFF(PHDASTATE) PHDASTATECC;


/** @def HDA_PROCESS_INTERRUPT
 * Wrapper around hdaProcessInterrupt that supplies the source function name
 * string in logging builds. */
#if defined(LOG_ENABLED) || defined(DOXYGEN_RUNNING)
void hdaProcessInterrupt(PPDMDEVINS pDevIns, PHDASTATE pThis, const char *pszSource);
# define HDA_PROCESS_INTERRUPT(a_pDevIns, a_pThis)  hdaProcessInterrupt((a_pDevIns), (a_pThis), __FUNCTION__)
#else
void hdaProcessInterrupt(PPDMDEVINS pDevIns, PHDASTATE pThis);
# define HDA_PROCESS_INTERRUPT(a_pDevIns, a_pThis)  hdaProcessInterrupt((a_pDevIns), (a_pThis))
#endif

/**
 * Returns the audio direction of a specified stream descriptor.
 *
 * The register layout specifies that input streams (SDI) come first,
 * followed by the output streams (SDO). So every stream ID below HDA_MAX_SDI
 * is an input stream, whereas everything >= HDA_MAX_SDI is an output stream.
 *
 * @note SDnFMT register does not provide that information, so we have to judge
 *       for ourselves.
 *
 * @return  Audio direction.
 * @param   uSD     The stream number.
 */
DECLINLINE(PDMAUDIODIR) hdaGetDirFromSD(uint8_t uSD)
{
    if (uSD < HDA_MAX_SDI)
        return PDMAUDIODIR_IN;
    AssertReturn(uSD < HDA_MAX_STREAMS, PDMAUDIODIR_UNKNOWN);
    return PDMAUDIODIR_OUT;
}

/* Used by hdaR3StreamSetUp: */
uint8_t hdaSDFIFOWToBytes(uint16_t u16RegFIFOW);

#if defined(VBOX_STRICT) && defined(VBOX_HDA_CAN_ACCESS_REG_MAP)
/* Only in DevHda.cpp: */
DECLINLINE(uint32_t *) hdaStrictRegAccessor(PHDASTATE pThis, uint32_t idxMap, uint32_t idxReg);
DECLINLINE(uint32_t *) hdaStrictStreamRegAccessor(PHDASTATE pThis, uint32_t idxMap0, uint32_t idxReg0, size_t idxStream);
#endif /* VBOX_STRICT && VBOX_HDA_CAN_ACCESS_REG_MAP */


/** @name HDA device functions used by the codec.
 * @{ */
DECLHIDDEN(int)     hdaR3MixerAddStream(PHDACODECR3 pCodec, PDMAUDIOMIXERCTL enmMixerCtl, PCPDMAUDIOSTREAMCFG pCfg);
DECLHIDDEN(int)     hdaR3MixerRemoveStream(PHDACODECR3 pCodec, PDMAUDIOMIXERCTL enmMixerCtl, bool fImmediate);
DECLHIDDEN(int)     hdaR3MixerControl(PHDACODECR3 pCodec, PDMAUDIOMIXERCTL enmMixerCtl, uint8_t uSD, uint8_t uChannel);
DECLHIDDEN(int)     hdaR3MixerSetVolume(PHDACODECR3 pCodec, PDMAUDIOMIXERCTL enmMixerCtl, PPDMAUDIOVOLUME pVol);
/** @} */


/** @name Saved state versions for the HDA device
 * @{ */
/** The current staved state version.
 * @note Only for the registration call.  Never used for tests. */
#define HDA_SAVED_STATE_VERSION         HDA_SAVED_STATE_WITHOUT_PERIOD

/** Removed period and redefined wall clock. */
#define HDA_SAVED_STATE_WITHOUT_PERIOD  8
/** Added (Controller):              Current wall clock value (this independent from WALCLK register value).
  * Added (Controller):              Current IRQ level.
  * Added (Per stream):              Ring buffer. This is optional and can be skipped if (not) needed.
  * Added (Per stream):              Struct g_aSSMStreamStateFields7.
  * Added (Per stream):              Struct g_aSSMStreamPeriodFields7.
  * Added (Current BDLE per stream): Struct g_aSSMBDLEDescFields7.
  * Added (Current BDLE per stream): Struct g_aSSMBDLEStateFields7. */
#define HDA_SAVED_STATE_VERSION_7       7
/** Saves the current BDLE state.
 * @since 5.0.14 (r104839) */
#define HDA_SAVED_STATE_VERSION_6       6
/** Introduced dynamic number of streams + stream identifiers for serialization.
 *  Bug: Did not save the BDLE states correctly.
 *  Those will be skipped on load then.
 * @since 5.0.12 (r104520)  */
#define HDA_SAVED_STATE_VERSION_5       5
/** Since this version the number of MMIO registers can be flexible. */
#define HDA_SAVED_STATE_VERSION_4       4
#define HDA_SAVED_STATE_VERSION_3       3
#define HDA_SAVED_STATE_VERSION_2       2
#define HDA_SAVED_STATE_VERSION_1       1
/** @} */

#endif /* !VBOX_INCLUDED_SRC_Audio_DevHda_h */

