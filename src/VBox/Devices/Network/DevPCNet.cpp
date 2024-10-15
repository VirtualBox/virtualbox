/* $Id: DevPCNet.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPCNet - AMD PCnet-PCI II / PCnet-FAST III (Am79C970A / Am79C973) Ethernet Controller Emulation.
 *
 * This software was written to be compatible with the specifications:
 *      AMD Am79C970A PCnet-PCI II Ethernet Controller Data-Sheet
 *      AMD Publication# 19436  Rev:E  Amendment/0  Issue Date: June 2000
 * and
 *      AMD Am79C973/Am79C975 PCnet-FAST III Single-Chip 10/100 Mbps PCI Ethernet Controller datasheet
 *      AMD publication# 20510  Rev:E  Amendment/0  Issue Date: August 2000
 * and
 *      AMD Am79C960 PCnet-ISA Single-Chip Ethernet Controller datasheet
 *      AMD publication# 16907  Rev:B  Amendment/0  Issue Date: May 1994
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
 * AMD PCnet-PCI II (Am79C970A) emulation
 *
 * Copyright (c) 2004 Antony T Curtis
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_PCNET
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pgm.h>
#include <VBox/version.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/net.h>
#include <iprt/string.h>
#include <iprt/time.h>
#ifdef IN_RING3
# include <iprt/mem.h>
# include <iprt/semaphore.h>
# include <iprt/uuid.h>
#endif

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/* Enable to handle frequent io reads in the guest context (recommended) */
#define PCNET_GC_ENABLED

/* PCNET_DEBUG_IO     - Log6 */
/* PCNET_DEBUG_BCR    - Log7 */
/* PCNET_DEBUG_CSR    - Log8 */
/* PCNET_DEBUG_RMD    - Log9 */
/* PCNET_DEBUG_TMD    - Log10 */
/* PCNET_DEBUG_MATCH  - Log11 */
/* PCNET_DEBUG_MII    - Log12 */

#define PCNET_IOPORT_SIZE               0x20
#define PCNET_PNPMMIO_SIZE              0x20

#define PCNET_SAVEDSTATE_VERSION        10

#define BCR_MAX_RAP                     50
#define MII_MAX_REG                     32
#define CSR_MAX_REG                     128

/** Maximum number of times we report a link down to the guest (failure to send frame) */
#define PCNET_MAX_LINKDOWN_REPORTED     3

/** Maximum frame size we handle */
#define MAX_FRAME                       1536

#define PCNET_INST_NR                   (pThis->iInstance)

/** @name Bus configuration registers
 * @{ */
#define BCR_MSRDA       0
#define BCR_MSWRA       1
#define BCR_MC          2
#define BCR_RESERVED3   3
#define BCR_LNKST       4
#define BCR_LED1        5
#define BCR_LED2        6
#define BCR_LED3        7
#define BCR_RESERVED8   8
#define BCR_FDC         9
/* 10 - 15 = reserved */
#define BCR_IOBASEL     16  /* Reserved */
#define BCR_IOBASEU     16  /* Reserved */
#define BCR_BSBC        18
#define BCR_EECAS       19
#define BCR_SWS         20
#define BCR_INTCON      21  /* Reserved */
#define BCR_PLAT        22
#define BCR_PCISVID     23
#define BCR_PCISID      24
#define BCR_SRAMSIZ     25
#define BCR_SRAMB       26
#define BCR_SRAMIC      27
#define BCR_EBADDRL     28
#define BCR_EBADDRU     29
#define BCR_EBD         30
#define BCR_STVAL       31
#define BCR_MIICAS      32
#define BCR_MIIADDR     33
#define BCR_MIIMDR      34
#define BCR_PCIVID      35
#define BCR_PMC_A       36
#define BCR_DATA0       37
#define BCR_DATA1       38
#define BCR_DATA2       39
#define BCR_DATA3       40
#define BCR_DATA4       41
#define BCR_DATA5       42
#define BCR_DATA6       43
#define BCR_DATA7       44
#define BCR_PMR1        45
#define BCR_PMR2        46
#define BCR_PMR3        47
/** @}  */

/** @name Bus configuration sub register accessors.
 * @{ */
#define BCR_DWIO(S)      !!((S)->aBCR[BCR_BSBC] & 0x0080)
#define BCR_SSIZE32(S)   !!((S)->aBCR[BCR_SWS ] & 0x0100)
#define BCR_SWSTYLE(S)     ((S)->aBCR[BCR_SWS ] & 0x00FF)
/** @} */

/** @name CSR subregister accessors.
 * @{ */
#define CSR_INIT(S)      !!((S)->aCSR[0] & 0x0001)  /**< Init assertion */
#define CSR_STRT(S)      !!((S)->aCSR[0] & 0x0002)  /**< Start assertion */
#define CSR_STOP(S)      !!((S)->aCSR[0] & 0x0004)  /**< Stop assertion */
#define CSR_TDMD(S)      !!((S)->aCSR[0] & 0x0008)  /**< Transmit demand. (perform xmit poll now (readable, settable, not clearable) */
#define CSR_TXON(S)      !!((S)->aCSR[0] & 0x0010)  /**< Transmit on (readonly) */
#define CSR_RXON(S)      !!((S)->aCSR[0] & 0x0020)  /**< Receive On */
#define CSR_INEA(S)      !!((S)->aCSR[0] & 0x0040)  /**< Interrupt Enable */
#define CSR_LAPPEN(S)    !!((S)->aCSR[3] & 0x0020)  /**< Look Ahead Packet Processing Enable */
#define CSR_DXSUFLO(S)   !!((S)->aCSR[3] & 0x0040)  /**< Disable Transmit Stop on Underflow error */
#define CSR_ASTRP_RCV(S) !!((S)->aCSR[4] & 0x0400)  /**< Auto Strip Receive */
#define CSR_DPOLL(S)     !!((S)->aCSR[4] & 0x1000)  /**< Disable Transmit Polling */
#define CSR_SPND(S)      !!((S)->aCSR[5] & 0x0001)  /**< Suspend */
#define CSR_LTINTEN(S)   !!((S)->aCSR[5] & 0x4000)  /**< Last Transmit Interrupt Enable */
#define CSR_TOKINTD(S)   !!((S)->aCSR[5] & 0x8000)  /**< Transmit OK Interrupt Disable */

#define CSR_STINT        !!((S)->aCSR[7] & 0x0800)  /**< Software Timer Interrupt */
#define CSR_STINTE       !!((S)->aCSR[7] & 0x0400)  /**< Software Timer Interrupt Enable */

#define CSR_DRX(S)       !!((S)->aCSR[15] & 0x0001) /**< Disable Receiver */
#define CSR_DTX(S)       !!((S)->aCSR[15] & 0x0002) /**< Disable Transmit */
#define CSR_LOOP(S)      !!((S)->aCSR[15] & 0x0004) /**< Loopback Enable */
#define CSR_DRCVPA(S)    !!((S)->aCSR[15] & 0x2000) /**< Disable Receive Physical Address */
#define CSR_DRCVBC(S)    !!((S)->aCSR[15] & 0x4000) /**< Disable Receive Broadcast */
#define CSR_PROM(S)      !!((S)->aCSR[15] & 0x8000) /**< Promiscuous Mode */
/** @} */

#if defined(RT_BIG_ENDIAN) || !defined(RT_LITTLE_ENDIAN)
# error fix macros (and more in this file) for big-endian machines
#endif

/** @name CSR register accessors.
 * @{ */
#define CSR_IADR(S)  (*(uint32_t*)((S)->aCSR +  1)) /**< Initialization Block Address */
#define CSR_CRBA(S)  (*(uint32_t*)((S)->aCSR + 18)) /**< Current Receive Buffer Address */
#define CSR_CXBA(S)  (*(uint32_t*)((S)->aCSR + 20)) /**< Current Transmit Buffer Address */
#define CSR_NRBA(S)  (*(uint32_t*)((S)->aCSR + 22)) /**< Next Receive Buffer Address */
#define CSR_BADR(S)  (*(uint32_t*)((S)->aCSR + 24)) /**< Base Address of Receive Ring */
#define CSR_NRDA(S)  (*(uint32_t*)((S)->aCSR + 26)) /**< Next Receive Descriptor Address */
#define CSR_CRDA(S)  (*(uint32_t*)((S)->aCSR + 28)) /**< Current Receive Descriptor Address */
#define CSR_BADX(S)  (*(uint32_t*)((S)->aCSR + 30)) /**< Base Address of Transmit Descriptor */
#define CSR_NXDA(S)  (*(uint32_t*)((S)->aCSR + 32)) /**< Next Transmit Descriptor Address */
#define CSR_CXDA(S)  (*(uint32_t*)((S)->aCSR + 34)) /**< Current Transmit Descriptor Address */
#define CSR_NNRD(S)  (*(uint32_t*)((S)->aCSR + 36)) /**< Next Next Receive Descriptor Address */
#define CSR_NNXD(S)  (*(uint32_t*)((S)->aCSR + 38)) /**< Next Next Transmit Descriptor Address */
#define CSR_CRBC(S)  ((S)->aCSR[40])                /**< Current Receive Byte Count */
#define CSR_CRST(S)  ((S)->aCSR[41])                /**< Current Receive Status */
#define CSR_CXBC(S)  ((S)->aCSR[42])                /**< Current Transmit Byte Count */
#define CSR_CXST(S)  ((S)->aCSR[43])                /**< Current transmit status */
#define CSR_NRBC(S)  ((S)->aCSR[44])                /**< Next Receive Byte Count */
#define CSR_NRST(S)  ((S)->aCSR[45])                /**< Next Receive Status */
#define CSR_POLL(S)  ((S)->aCSR[46])                /**< Transmit Poll Time Counter */
#define CSR_PINT(S)  ((S)->aCSR[47])                /**< Transmit Polling Interval */
#define CSR_PXDA(S)  (*(uint32_t*)((S)->aCSR + 60)) /**< Previous Transmit Descriptor Address*/
#define CSR_PXBC(S)  ((S)->aCSR[62])                /**< Previous Transmit Byte Count */
#define CSR_PXST(S)  ((S)->aCSR[63])                /**< Previous Transmit Status */
#define CSR_NXBA(S)  (*(uint32_t*)((S)->aCSR + 64)) /**< Next Transmit Buffer Address */
#define CSR_NXBC(S)  ((S)->aCSR[66])                /**< Next Transmit Byte Count */
#define CSR_NXST(S)  ((S)->aCSR[67])                /**< Next Transmit Status */
#define CSR_RCVRC(S) ((S)->aCSR[72])                /**< Receive Descriptor Ring Counter */
#define CSR_XMTRC(S) ((S)->aCSR[74])                /**< Transmit Descriptor Ring Counter */
#define CSR_RCVRL(S) ((S)->aCSR[76])                /**< Receive Descriptor Ring Length */
#define CSR_XMTRL(S) ((S)->aCSR[78])                /**< Transmit Descriptor Ring Length */
#define CSR_MISSC(S) ((S)->aCSR[112])               /**< Missed Frame Count */
/** @} */

/** @name Version for the PCnet/FAST III 79C973 card
 * @{ */
#define CSR_VERSION_LOW_79C973  0x5003  /* the lower two bits are always 11b for AMD (JEDEC) */
#define CSR_VERSION_LOW_79C970A 0x1003
#define CSR_VERSION_LOW_79C960  0x3003
#define CSR_VERSION_HIGH        0x0262
/** @}  */

/** Calculates the full physical address.  */
#define PHYSADDR(S,A) ((A) | (S)->GCUpperPhys)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Emulated device types.
 */
enum PCNET_DEVICE_TYPE
{
    DEV_AM79C970A   = 0,    /* PCnet-PCI II (PCI, 10 Mbps). */
    DEV_AM79C973    = 1,    /* PCnet-FAST III (PCI, 10/100 Mbps). */
    DEV_AM79C960    = 2,    /* PCnet-ISA (ISA, 10 Mbps, NE2100/NE1500T compatible) */
    DEV_AM79C960_EB = 3     /* PCnet-ISA (ISA, 10 Mbps, Racal InterLan EtherBlaster compatible) */
};

#define PCNET_IS_PCI(pcnet) ((pcnet->uDevType == DEV_AM79C970A) || (pcnet->uDevType == DEV_AM79C973))
#define PCNET_IS_ISA(pcnet) ((pcnet->uDevType == DEV_AM79C960) || (pcnet->uDevType == DEV_AM79C960_EB))

/*
 *  Note: Old adapters, including the original NE2100/NE1500T, used the LANCE (Am7990) or
 *  possibly C-LANCE (Am79C90) chips. Those only had the RAP/RDP register and any PROMs were
 *  decoded by external circuitry; as a consequence, different vendors used different layouts.
 *  Several different variants are known:
 *
 *   1) Racal Interlan NI6510 (before PCnet-based EtherBlaster variants):
 *      - RDP     at iobase + 0x00 (16-bit)
 *      - RAP     at iobase + 0x02 (16-bit)
 *      - reset   at iobase + 0x04 (8-bit)
 *      - config  at iobase + 0x05 (8-bit)
 *      - PROM    at iobase + 0x08
 *
 *   2) BICC Data Networks ISOLAN 4110-2 (ISA) / 4110-3 (MCA):
 *      - PROM    at iobase + 0x00
 *      - reset   at iobase + 0x04
 *      - config  at iobase + 0x05
 *      - RDP     at iobase + 0x0c (16-bit)
 *      - RAP     at iobase + 0x0e (16-bit)
 *
 *   3) Novell NE2100/NE1500T, AMD Am2100, all PCnet chips:
 *      - PROM    at iobase + 0x00
 *      - RDP     at iobase + 0x10 (16-bit)
 *      - RAP     at iobase + 0x12 (16-bit)
 *      - reset   at iobase + 0x14 (16-bit)
 *      - BDP/IDP at iobase + 0x16 (16-bit)
 *
 * There were also non-busmastering LANCE adapters, such as the BICC 4110-1, DEC DEPCA,
 * or NTI 16. Those are uninteresting.
 *
 * Newer adapters based on the integrated PCnet-ISA (Am79C960) and later have a fixed
 * register layout compatible with the NE2100. However, they may still require different
 * drivers. At least two different incompatible drivers exist for PCnet-based cards:
 * Those from AMD and those from Racal InterLan for their EtherBlaster cards. The PROM
 * on EtherBlaster cards uses a different signature ('RD' instead of 'WW') and Racal's
 * drivers insist on the MAC address having the Racal-Datacom OUI (02 07 01).
 */

/**
 * PCNET state.
 *
 * @implements  PDMIBASE
 * @implements  PDMINETWORKDOWN
 * @implements  PDMINETWORKCONFIG
 * @implements  PDMILEDPORTS
 */
typedef struct PCNETSTATE
{
    /** Software Interrupt timer - R3. */
    TMTIMERHANDLE                       hTimerSoftInt;
    /** Poll timer - R3. */
    TMTIMERHANDLE                       hTimerPoll;
    /** Restore timer.
     *  This is used to disconnect and reconnect the link after a restore. */
    TMTIMERHANDLE                       hTimerRestore;

    /** Transmit signaller. */
    PDMTASKHANDLE                       hXmitTask;

    /** Register Address Pointer */
    uint32_t                            u32RAP;
    /** Internal interrupt service */
    int32_t                             iISR;
    /** ??? */
    uint32_t                            u32Lnkst;
    /** Address of the RX descriptor table (ring). Loaded at init. */
    RTGCPHYS32                          GCRDRA;
    /** Address of the TX descriptor table (ring). Loaded at init. */
    RTGCPHYS32                          GCTDRA;
    uint8_t                             aPROM[16];
    uint16_t                            aCSR[CSR_MAX_REG];
    uint16_t                            aBCR[BCR_MAX_RAP];
    uint16_t                            aMII[MII_MAX_REG];

    /** Holds the bits which were really seen by the guest. Relevant are bits
     * 8..14 (IDON, TINT, RINT, MERR, MISS, CERR, BABL). We don't allow the
     * guest to clear any of these bits (by writing a ONE) before a bit was
     * seen by the guest. */
    uint16_t                            u16CSR0LastSeenByGuest;
    /** The configured IRQ for ISA operation. */
    uint8_t                             uIsaIrq;
    /** Alignment padding. */
    uint8_t                             Alignment2[1+4];

    /** Last time we polled the queues */
    uint64_t                            u64LastPoll;

    /** Size of a RX/TX descriptor (8 or 16 bytes according to SWSTYLE */
    int32_t                             iLog2DescSize;
    /** Bits 16..23 in 16-bit mode */
    RTGCPHYS32                          GCUpperPhys;

    /** Base port of the I/O space region. */
    RTIOPORT                            IOPortBase;
    /** If set the link is currently up. */
    bool                                fLinkUp;
    /** If set the link is temporarily down because of a saved state load. */
    bool                                fLinkTempDown;

    /** Number of times we've reported the link down. */
    uint32_t                            cLinkDownReported;
    /** The configured MAC address. */
    RTMAC                               MacConfigured;
    /** Set if PCNETSTATER3::pDrvR3 is not NULL. */
    bool                                fDriverAttached;
    /** Alignment padding. */
    uint8_t                             bAlignment3;

    /** The LED. */
    PDMLED                              Led;

    /** Access critical section. */
    PDMCRITSECT                         CritSect;
    /** Event semaphore for blocking on receive. */
    SUPSEMEVENT                         hEventOutOfRxSpace;
    /** We are waiting/about to start waiting for more receive buffers. */
    bool volatile                       fMaybeOutOfSpace;
    /** True if we signal the guest that RX packets are missing. */
    bool                                fSignalRxMiss;
    /** Alignment padding. */
    uint8_t                             Alignment4[HC_ARCH_BITS == 64 ? 2 : 6];

    /** Error counter for bad receive descriptors. */
    uint32_t                            uCntBadRMD;
    /** Emulated device type. */
    uint8_t                             uDevType;
    bool                                afAlignment5[3];
    /** Link speed to be reported through CSR68. */
    uint32_t                            u32LinkSpeed;
    /** MS to wait before we enable the link. */
    uint32_t                            cMsLinkUpDelay;
    /** The device instance number (for logging). */
    uint32_t                            iInstance;

    /** PCI Region \#0: I/O ports offset 0x10-0x1f. */
    IOMIOPORTHANDLE                     hIoPortsPci;
    /** PCI Region \#0: I/O ports offset 0x00-0x0f. */
    IOMIOPORTHANDLE                     hIoPortsPciAProm;
    /** PCI Region \#1: MMIO alternative to the I/O ports in region \#0. */
    IOMMMIOHANDLE                       hMmioPci;

    /** ISA I/O ports offset 0x10-0x1f. */
    IOMIOPORTHANDLE                     hIoPortsIsa;
    /** ISA I/O ports offset 0x00-0x0f. */
    IOMIOPORTHANDLE                     hIoPortsIsaAProm;

    /** Backwards compatible shared memory region during state loading (before 4.3.6). */
    PGMMMIO2HANDLE                      hMmio2Shared;

    /** The loopback transmit buffer (avoid stack allocations). */
    uint8_t                             abLoopBuf[4096];
    /** The recv buffer. */
    uint8_t                             abRecvBuf[4096];

    STAMCOUNTER                         StatReceiveBytes;
    STAMCOUNTER                         StatTransmitBytes;
#ifdef VBOX_WITH_STATISTICS
    STAMPROFILEADV                      StatMMIOReadRZ;
    STAMPROFILEADV                      StatMMIOReadR3;
    STAMPROFILEADV                      StatMMIOWriteRZ;
    STAMPROFILEADV                      StatMMIOWriteR3;
    STAMPROFILEADV                      StatAPROMRead;
    STAMPROFILEADV                      StatAPROMWrite;
    STAMPROFILEADV                      StatIOReadRZ;
    STAMPROFILEADV                      StatIOReadR3;
    STAMPROFILEADV                      StatIOWriteRZ;
    STAMPROFILEADV                      StatIOWriteR3;
    STAMPROFILEADV                      StatTimer;
    STAMPROFILEADV                      StatReceive;
    STAMPROFILEADV                      StatTransmitR3;
    STAMPROFILEADV                      StatTransmitRZ;
    STAMCOUNTER                         StatTransmitCase1;
    STAMCOUNTER                         StatTransmitCase2;
    STAMPROFILE                         StatTransmitSendR3;
    STAMPROFILE                         StatTransmitSendRZ;
    STAMPROFILEADV                      StatTdtePollRZ;
    STAMPROFILEADV                      StatTdtePollR3;
    STAMPROFILEADV                      StatTmdStoreRZ;
    STAMPROFILEADV                      StatTmdStoreR3;
    STAMPROFILEADV                      StatRdtePollR3;
    STAMPROFILEADV                      StatRdtePollRZ;
    STAMPROFILE                         StatRxOverflow;
    STAMCOUNTER                         StatRxOverflowWakeup;
    STAMCOUNTER                         aStatXmitFlush[16];
    STAMCOUNTER                         aStatXmitChainCounts[16];
    STAMCOUNTER                         StatXmitSkipCurrent;
    STAMPROFILEADV                      StatInterrupt;
    STAMPROFILEADV                      StatPollTimer;
    STAMCOUNTER                         StatMIIReads;
#endif /* VBOX_WITH_STATISTICS */
} PCNETSTATE;
/** Pointer to a shared PCnet state structure. */
typedef PCNETSTATE *PPCNETSTATE;


/**
 * PCNET state for ring-3.
 *
 * @implements  PDMIBASE
 * @implements  PDMINETWORKDOWN
 * @implements  PDMINETWORKCONFIG
 * @implements  PDMILEDPORTS
 */
typedef struct PCNETSTATER3
{
    /** Pointer to the device instance. */
    PPDMDEVINSR3                        pDevIns;
    /** Pointer to the connector of the attached network driver. */
    PPDMINETWORKUPR3                    pDrv;
    /** Pointer to the attached network driver. */
    R3PTRTYPE(PPDMIBASE)                pDrvBase;
    /** LUN\#0 + status LUN: The base interface. */
    PDMIBASE                            IBase;
    /** LUN\#0: The network port interface. */
    PDMINETWORKDOWN                     INetworkDown;
    /** LUN\#0: The network config port interface. */
    PDMINETWORKCONFIG                   INetworkConfig;

    /** Status LUN: The LED ports. */
    PDMILEDPORTS                        ILeds;
    /** Partner of ILeds. */
    R3PTRTYPE(PPDMILEDCONNECTORS)       pLedsConnector;
} PCNETSTATER3;
/** Pointer to a PCnet state structure for ring-3. */
typedef PCNETSTATER3 *PPCNETSTATER3;


/**
 * PCNET state for ring-0.
 */
typedef struct PCNETSTATER0
{
    /** Pointer to the connector of the attached network driver. */
    PPDMINETWORKUPR0                    pDrv;
} PCNETSTATER0;
/** Pointer to a PCnet state structure for ring-0. */
typedef PCNETSTATER0 *PPCNETSTATER0;


/**
 * PCNET state for raw-mode.
 */
typedef struct PCNETSTATERC
{
    /** Pointer to the connector of the attached network driver. */
    PPDMINETWORKUPRC                    pDrv;
} PCNETSTATERC;
/** Pointer to a PCnet state structure for raw-mode. */
typedef PCNETSTATERC *PPCNETSTATERC;


/** The PCnet state structure for the current context. */
typedef CTX_SUFF(PCNETSTATE) PCNETSTATECC;
/** Pointer to a PCnet state structure for the current context. */
typedef CTX_SUFF(PPCNETSTATE) PPCNETSTATECC;


/** @todo All structs: big endian? */

struct INITBLK16
{
    uint16_t mode;      /**< copied into csr15 */
    uint16_t padr1;     /**< MAC  0..15 */
    uint16_t padr2;     /**< MAC 16..32 */
    uint16_t padr3;     /**< MAC 33..47 */
    uint16_t ladrf1;    /**< logical address filter  0..15 */
    uint16_t ladrf2;    /**< logical address filter 16..31 */
    uint16_t ladrf3;    /**< logical address filter 32..47 */
    uint16_t ladrf4;    /**< logical address filter 48..63 */
    uint32_t rdra:24;   /**< address of receive descriptor ring */
    uint32_t res1:5;    /**< reserved */
    uint32_t rlen:3;    /**< number of receive descriptor ring entries */
    uint32_t tdra:24;   /**< address of transmit descriptor ring */
    uint32_t res2:5;    /**< reserved */
    uint32_t tlen:3;    /**< number of transmit descriptor ring entries */
};
AssertCompileSize(INITBLK16, 24);

/** bird:  I've changed the type for the bitfields. They should only be 16-bit all together.
 *  frank: I've changed the bitfiled types to uint32_t to prevent compiler warnings. */
struct INITBLK32
{
    uint16_t mode;      /**< copied into csr15 */
    uint16_t res1:4;    /**< reserved */
    uint16_t rlen:4;    /**< number of receive descriptor ring entries */
    uint16_t res2:4;    /**< reserved */
    uint16_t tlen:4;    /**< number of transmit descriptor ring entries */
    uint16_t padr1;     /**< MAC  0..15 */
    uint16_t padr2;     /**< MAC 16..31 */
    uint16_t padr3;     /**< MAC 32..47 */
    uint16_t res3;      /**< reserved */
    uint16_t ladrf1;    /**< logical address filter  0..15 */
    uint16_t ladrf2;    /**< logical address filter 16..31 */
    uint16_t ladrf3;    /**< logical address filter 32..47 */
    uint16_t ladrf4;    /**< logical address filter 48..63 */
    uint32_t rdra;      /**< address of receive descriptor ring */
    uint32_t tdra;      /**< address of transmit descriptor ring */
};
AssertCompileSize(INITBLK32, 28);

/** Transmit Message Descriptor */
typedef struct TMD
{
    struct
    {
        uint32_t tbadr;         /**< transmit buffer address */
    } tmd0;
    struct
    {
        uint32_t bcnt:12;       /**< buffer byte count (two's complement) */
        uint32_t ones:4;        /**< must be 1111b */
        uint32_t res:7;         /**< reserved */
        uint32_t bpe:1;         /**< bus parity error */
        uint32_t enp:1;         /**< end of packet */
        uint32_t stp:1;         /**< start of packet */
        uint32_t def:1;         /**< deferred */
        uint32_t one:1;         /**< exactly one retry was needed to transmit a frame */
        uint32_t ltint:1;       /**< suppress interrupts after successful transmission */
        uint32_t nofcs:1;       /**< when set, the state of DXMTFCS is ignored and
                                     transmitter FCS generation is activated. */
        uint32_t err:1;         /**< error occurred */
        uint32_t own:1;         /**< 0=owned by guest driver, 1=owned by controller */
    } tmd1;
    struct
    {
        uint32_t trc:4;         /**< transmit retry count */
        uint32_t res:12;        /**< reserved */
        uint32_t tdr:10;        /**< time domain reflectometry */
        uint32_t rtry:1;        /**< retry error */
        uint32_t lcar:1;        /**< loss of carrier */
        uint32_t lcol:1;        /**< late collision */
        uint32_t exdef:1;       /**< excessive deferral */
        uint32_t uflo:1;        /**< underflow error */
        uint32_t buff:1;        /**< out of buffers (ENP not found) */
    } tmd2;
    struct
    {
        uint32_t res;           /**< reserved for user defined space */
    } tmd3;
} TMD;
AssertCompileSize(TMD, 16);

/** Receive Message Descriptor */
typedef struct RMD
{
    struct
    {
        uint32_t rbadr;         /**< receive buffer address */
    } rmd0;
    struct
    {
        uint32_t bcnt:12;       /**< buffer byte count (two's complement) */
        uint32_t ones:4;        /**< must be 1111b */
        uint32_t res:4;         /**< reserved */
        uint32_t bam:1;         /**< broadcast address match */
        uint32_t lafm:1;        /**< logical filter address match */
        uint32_t pam:1;         /**< physical address match */
        uint32_t bpe:1;         /**< bus parity error */
        uint32_t enp:1;         /**< end of packet */
        uint32_t stp:1;         /**< start of packet */
        uint32_t buff:1;        /**< buffer error */
        uint32_t crc:1;         /**< crc error on incoming frame */
        uint32_t oflo:1;        /**< overflow error (lost all or part of incoming frame) */
        uint32_t fram:1;        /**< frame error */
        uint32_t err:1;         /**< error occurred */
        uint32_t own:1;         /**< 0=owned by guest driver, 1=owned by controller */
    } rmd1;
    struct
    {
        uint32_t mcnt:12;       /**< message byte count */
        uint32_t zeros:4;       /**< 0000b */
        uint32_t rpc:8;         /**< receive frame tag */
        uint32_t rcc:8;         /**< receive frame tag + reserved */
    } rmd2;
    struct
    {
        uint32_t res;           /**< reserved for user defined space */
    } rmd3;
} RMD;
AssertCompileSize(RMD, 16);


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void     pcnetPollRxTx(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC);
static void     pcnetPollTimer(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC);
static void     pcnetUpdateIrq(PPDMDEVINS pDevIns, PPCNETSTATE pThis);
static uint32_t pcnetBCRReadU16(PPCNETSTATE pThis, uint32_t u32RAP);
static VBOXSTRICTRC pcnetBCRWriteU16(PPDMDEVINS pDevIns, PPCNETSTATE pThis, uint32_t u32RAP, uint32_t val);
static void     pcnetPollTimerStart(PPDMDEVINS pDevIns, PPCNETSTATE pThis);
static int      pcnetXmitPending(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, bool fOnWorkerThread);

#define PRINT_TMD(T) Log10((    \
        "TMD0 : TBADR=%#010x\n" \
        "TMD1 : OWN=%d, ERR=%d, FCS=%d, LTI=%d, "       \
        "ONE=%d, DEF=%d, STP=%d, ENP=%d,\n"             \
        "       BPE=%d, BCNT=%d\n"                      \
        "TMD2 : BUF=%d, UFL=%d, EXD=%d, LCO=%d, "       \
        "LCA=%d, RTR=%d,\n"                             \
        "       TDR=%d, TRC=%d\n",                      \
        (T)->tmd0.tbadr,                                \
        (T)->tmd1.own, (T)->tmd1.err, (T)->tmd1.nofcs,  \
        (T)->tmd1.ltint, (T)->tmd1.one, (T)->tmd1.def,  \
        (T)->tmd1.stp, (T)->tmd1.enp, (T)->tmd1.bpe,    \
        4096-(T)->tmd1.bcnt,                            \
        (T)->tmd2.buff, (T)->tmd2.uflo, (T)->tmd2.exdef,\
        (T)->tmd2.lcol, (T)->tmd2.lcar, (T)->tmd2.rtry, \
        (T)->tmd2.tdr, (T)->tmd2.trc))

#define PRINT_RMD(R) Log9((    \
        "RMD0 : RBADR=%#010x\n" \
        "RMD1 : OWN=%d, ERR=%d, FRAM=%d, OFLO=%d, "     \
        "CRC=%d, BUFF=%d, STP=%d, ENP=%d,\n       "     \
        "BPE=%d, PAM=%d, LAFM=%d, BAM=%d, ONES=%d, BCNT=%d\n" \
        "RMD2 : RCC=%d, RPC=%d, MCNT=%d, ZEROS=%d\n",   \
        (R)->rmd0.rbadr,                                \
        (R)->rmd1.own, (R)->rmd1.err, (R)->rmd1.fram,   \
        (R)->rmd1.oflo, (R)->rmd1.crc, (R)->rmd1.buff,  \
        (R)->rmd1.stp, (R)->rmd1.enp, (R)->rmd1.bpe,    \
        (R)->rmd1.pam, (R)->rmd1.lafm, (R)->rmd1.bam,   \
        (R)->rmd1.ones, 4096-(R)->rmd1.bcnt,            \
        (R)->rmd2.rcc, (R)->rmd2.rpc, (R)->rmd2.mcnt,   \
        (R)->rmd2.zeros))



/**
 * Checks if the link is up.
 * @returns true if the link is up.
 * @returns false if the link is down.
 */
DECLINLINE(bool) pcnetIsLinkUp(PPCNETSTATE pThis)
{
    return pThis->fDriverAttached && !pThis->fLinkTempDown && pThis->fLinkUp;
}

/**
 * Memory write helper to handle PCI/ISA differences.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the PCNet device instance
 * @param   GCPhys      Guest physical memory address
 * @param   pvBuf       Host side buffer address
 * @param   cbWrite     Number of bytes to write
 */
static void pcnetPhysWrite(PPDMDEVINS pDevIns, PPCNETSTATE pThis, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite)
{
    if (!PCNET_IS_ISA(pThis))
        PDMDevHlpPCIPhysWrite(pDevIns, GCPhys, pvBuf, cbWrite);
    else
        PDMDevHlpPhysWrite(pDevIns, GCPhys, pvBuf, cbWrite);
}


/**
 * Memory read helper to handle PCI/ISA differences.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the PCNet device instance.
 * @param   GCPhys      Guest physical memory address.
 * @param   pvBuf       Host side buffer address.
 * @param   cbRead      Number of bytes to read.
 */
static void pcnetPhysRead(PPDMDEVINS pDevIns, PPCNETSTATE pThis, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead)
{
    if (!PCNET_IS_ISA(pThis))
        PDMDevHlpPCIPhysRead(pDevIns, GCPhys, pvBuf, cbRead);
    else
        PDMDevHlpPhysRead(pDevIns, GCPhys, pvBuf, cbRead);
}


/**
 * Load transmit message descriptor (TMD) if we own it.
 * Makes sure we read the OWN bit first, which requires issuing two reads if
 * the OWN bit is laid out in the second (D)WORD in memory.
 *
 * @param   pDevIns     The device instance.
 * @param pThis         adapter private data
 * @param addr          physical address of the descriptor
 * @return              true if we own the descriptor, false otherwise
 */
DECLINLINE(bool) pcnetTmdTryLoad(PPDMDEVINS pDevIns, PPCNETSTATE pThis, TMD *tmd, RTGCPHYS32 addr)
{
    /* Convert the in-memory format to the internal layout which corresponds to SWSTYLE=2.
     * Do not touch tmd if the OWN bit is not set (i.e. we don't own the descriptor).
     */
    if (RT_UNLIKELY(BCR_SWSTYLE(pThis) == 0))
    {
        uint16_t xda[4];    /* Corresponds to memory layout, last word unused. */

        /* For SWSTYLE=0, the OWN bit is in the second WORD we need and must be read before the first WORD. */
        pcnetPhysRead(pDevIns, pThis, addr + sizeof(uint16_t), (void*)&xda[1], 2 * sizeof(uint16_t));
        if (!(xda[1] & RT_BIT(15)))
            return false;
        pcnetPhysRead(pDevIns, pThis, addr, (void*)&xda[0], sizeof(uint16_t));
        ((uint32_t *)tmd)[0] = (uint32_t)xda[0] | ((uint32_t)(xda[1] & 0x00ff) << 16);  /* TMD0, buffer address. */
        ((uint32_t *)tmd)[1] = (uint32_t)xda[2] | ((uint32_t)(xda[1] & 0xff00) << 16);  /* TMD1, buffer size and control bits. */
        ((uint32_t *)tmd)[2] = 0;   /* TMD2, status bits, set on error but not read. */
        ((uint32_t *)tmd)[3] = 0;   /* TMD3, user data, never accessed. */
    }
    else if (RT_LIKELY(BCR_SWSTYLE(pThis) != 3))
    {
        /* For SWSTYLE=2, the OWN bit is in the second DWORD we need and must be read first. */
        uint32_t xda[2];
        pcnetPhysRead(pDevIns, pThis, addr + sizeof(uint32_t), (void*)&xda[1], sizeof(uint32_t));
        if (!(xda[1] & RT_BIT(31)))
            return false;
        pcnetPhysRead(pDevIns, pThis, addr, (void*)&xda[0], sizeof(uint32_t));
        ((uint32_t *)tmd)[0] = xda[0];  /* TMD0, buffer address. */
        ((uint32_t *)tmd)[1] = xda[1];  /* TMD1, buffer size and control bits. */
        ((uint32_t *)tmd)[2] = 0;       /* TMD2, status bits, set on error but not read. */
        ((uint32_t *)tmd)[3] = 0;       /* TMD3, user data, never accessed. */
    }
    else
    {
        /* For SWSTYLE=3, the OWN bit is in the first DWORD we need, therefore a single read suffices. */
        uint32_t xda[2];
        pcnetPhysRead(pDevIns, pThis, addr + sizeof(uint32_t), (void*)&xda, sizeof(xda));
        if (!(xda[0] & RT_BIT(31)))
            return false;
        ((uint32_t *)tmd)[0] = xda[1];  /* TMD0, buffer address. */
        ((uint32_t *)tmd)[1] = xda[0];  /* TMD1, buffer size and control bits. */
        ((uint32_t *)tmd)[2] = 0;       /* TMD2, status bits, set on error but not read. */
        ((uint32_t *)tmd)[3] = 0;       /* TMD3, user data, never accessed. */
    }

    return !!tmd->tmd1.own;
}

#if defined(IN_RING3) || defined(LOG_ENABLED)

/**
 * Loads an entire transmit message descriptor. Used for logging/debugging.
 *
 * @param pDevIns       The device instance.
 * @param pThis         Adapter private data
 * @param addr          Physical address of the descriptor
 */
DECLINLINE(void) pcnetTmdLoadAll(PPDMDEVINS pDevIns, PPCNETSTATE pThis, TMD *tmd, RTGCPHYS32 addr)
{
    /* Convert the in-memory format to the internal layout which corresponds to SWSTYLE=2. */
    if (RT_UNLIKELY(BCR_SWSTYLE(pThis) == 0))
    {
        uint16_t xda[4];

        /* For SWSTYLE=0, we have to do a bit of work. */
        pcnetPhysRead(pDevIns, pThis, addr, (void*)&xda, sizeof(xda));
        ((uint32_t *)tmd)[0] = (uint32_t)xda[0] | ((uint32_t)(xda[1] & 0x00ff) << 16);  /* TMD0, buffer address. */
        ((uint32_t *)tmd)[1] = (uint32_t)xda[2] | ((uint32_t)(xda[1] & 0xff00) << 16);  /* TMD1, buffer size and control bits. */
        ((uint32_t *)tmd)[2] = (uint32_t)xda[3] << 16;                                  /* TMD2, status bits. */
        ((uint32_t *)tmd)[3] = 0;                                           /* TMD3, user data, not present for SWSTYLE=1. */
    }
    else if (RT_LIKELY(BCR_SWSTYLE(pThis) != 3))
    {
        /* For SWSTYLE=2, simply read the TMD as is. */
        pcnetPhysRead(pDevIns, pThis, addr, (void*)tmd, sizeof(*tmd));
    }
    else
    {
        /* For SWSTYLE=3, swap the first and third DWORD around. */
        uint32_t xda[4];
        pcnetPhysRead(pDevIns, pThis, addr, (void*)&xda, sizeof(xda));
        ((uint32_t *)tmd)[0] = xda[2];  /* TMD0, buffer address. */
        ((uint32_t *)tmd)[1] = xda[1];  /* TMD1, buffer size and control bits. */
        ((uint32_t *)tmd)[2] = xda[0];  /* TMD2, status bits. */
        ((uint32_t *)tmd)[3] = xda[3];  /* TMD3, user data. */
    }
}

#endif

/**
 * Store transmit message descriptor and hand it over to the host (the VM guest).
 * Make sure the cleared OWN bit gets written last.
 */
DECLINLINE(void) pcnetTmdStorePassHost(PPDMDEVINS pDevIns, PPCNETSTATE pThis, TMD *tmd, RTGCPHYS32 addr)
{
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatTmdStore), a);
    if (RT_UNLIKELY(BCR_SWSTYLE(pThis) == 0))
    {
        uint16_t xda[4];    /* Corresponds to memory layout, only two words used. */

        /* For SWSTYLE=0, write the status word first, then the word containing the OWN bit. */
        xda[1] = ((((uint32_t *)tmd)[0] >> 16) &   0xff) | ((((uint32_t *)tmd)[1]>>16) & 0xff00);
        xda[1] &= ~RT_BIT(15);
        xda[3] =   ((uint32_t *)tmd)[2] >> 16;
        /** @todo The WORD containing status bits may not need to be written unless the ERR bit is set. */
        pcnetPhysWrite(pDevIns, pThis, addr + 3 * sizeof(uint16_t), (void*)&xda[3], sizeof(uint16_t));
        pcnetPhysWrite(pDevIns, pThis, addr + 1 * sizeof(uint16_t), (void*)&xda[1], sizeof(uint16_t));
    }
    else if (RT_LIKELY(BCR_SWSTYLE(pThis) != 3))
    {
        /* For SWSTYLE=0, write TMD2 first, then TMD1. */
        /** @todo The DWORD containing status bits may not need to be written unless the ERR bit is set. */
        pcnetPhysWrite(pDevIns, pThis, addr + 2 * sizeof(uint32_t), (uint32_t*)tmd + 2, sizeof(uint32_t));
        ((uint32_t*)tmd)[1] &= ~RT_BIT(31);
        pcnetPhysWrite(pDevIns, pThis, addr + 1 * sizeof(uint32_t), (uint32_t*)tmd + 1, sizeof(uint32_t));
    }
    else
    {
        uint32_t xda[2];

        /* For SWSTYLE=3, two DWORDs can be written in one go because the OWN bit is last. */
        xda[0] = ((uint32_t *)tmd)[2];
        xda[1] = ((uint32_t *)tmd)[1];
        xda[1] &= ~RT_BIT(31);
        pcnetPhysWrite(pDevIns, pThis, addr, (void*)&xda, sizeof(xda));
    }
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTmdStore), a);
}

/**
 * Load receive message descriptor
 *
 * Make sure we read the own flag first.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           Adapter private data.
 * @param   addr            Physical address of the descriptor.
 * @param   fRetIfNotOwn    Return immediately after reading the own flag if we
 *                          don't own the descriptor.
 * @returns true if we own the descriptor, false otherwise
 */
DECLINLINE(bool) pcnetRmdLoad(PPDMDEVINS pDevIns, PPCNETSTATE pThis, RMD *rmd, RTGCPHYS32 addr, bool fRetIfNotOwn)
{
    uint8_t    ownbyte;

    if (RT_UNLIKELY(BCR_SWSTYLE(pThis) == 0))
    {
        uint16_t rda[4];
        pcnetPhysRead(pDevIns, pThis, addr+3, &ownbyte, 1);
        if (!(ownbyte & 0x80) && fRetIfNotOwn)
            return false;
        pcnetPhysRead(pDevIns, pThis, addr, (void*)&rda[0], sizeof(rda));
        ((uint32_t *)rmd)[0] = (uint32_t)rda[0] | ((rda[1] & 0x00ff) << 16);
        ((uint32_t *)rmd)[1] = (uint32_t)rda[2] | ((rda[1] & 0xff00) << 16);
        ((uint32_t *)rmd)[2] = (uint32_t)rda[3];
        ((uint32_t *)rmd)[3] = 0;
    }
    else if (RT_LIKELY(BCR_SWSTYLE(pThis) != 3))
    {
        pcnetPhysRead(pDevIns, pThis, addr+7, &ownbyte, 1);
        if (!(ownbyte & 0x80) && fRetIfNotOwn)
            return false;
        pcnetPhysRead(pDevIns, pThis, addr, (void*)rmd, 16);
    }
    else
    {
        uint32_t rda[4];
        pcnetPhysRead(pDevIns, pThis, addr+7, &ownbyte, 1);
        if (!(ownbyte & 0x80) && fRetIfNotOwn)
            return false;
        pcnetPhysRead(pDevIns, pThis, addr, (void*)&rda[0], sizeof(rda));
        ((uint32_t *)rmd)[0] = rda[2];
        ((uint32_t *)rmd)[1] = rda[1];
        ((uint32_t *)rmd)[2] = rda[0];
        ((uint32_t *)rmd)[3] = rda[3];
    }
    /* Double check the own bit; guest drivers might be buggy and lock prefixes in the recompiler are ignored by other threads. */
#ifdef DEBUG
    if (rmd->rmd1.own == 1 && !(ownbyte & 0x80))
        Log(("pcnetRmdLoad: own bit flipped while reading!!\n"));
#endif
    if (!(ownbyte & 0x80))
        rmd->rmd1.own = 0;

    return !!rmd->rmd1.own;
}


/**
 * Store receive message descriptor and hand it over to the host (the VM guest).
 * Make sure that all data are transmitted before we clear the own flag.
 */
DECLINLINE(void) pcnetRmdStorePassHost(PPDMDEVINS pDevIns, PPCNETSTATE pThis, RMD *rmd, RTGCPHYS32 addr)
{
    if (RT_UNLIKELY(BCR_SWSTYLE(pThis) == 0))
    {
        uint16_t rda[4];
        rda[0] =   ((uint32_t *)rmd)[0]      & 0xffff;
        rda[1] = ((((uint32_t *)rmd)[0]>>16) &   0xff) | ((((uint32_t *)rmd)[1]>>16) & 0xff00);
        rda[2] =   ((uint32_t *)rmd)[1]      & 0xffff;
        rda[3] =   ((uint32_t *)rmd)[2]      & 0xffff;
        rda[1] |=  0x8000;
        pcnetPhysWrite(pDevIns, pThis, addr, (void*)&rda[0], sizeof(rda));
        rda[1] &= ~0x8000;
        pcnetPhysWrite(pDevIns, pThis, addr+3, (uint8_t*)rda + 3, 1);
    }
    else if (RT_LIKELY(BCR_SWSTYLE(pThis) != 3))
    {
        ((uint32_t*)rmd)[1] |=  0x80000000;
        pcnetPhysWrite(pDevIns, pThis, addr, (void*)rmd, 12);
        ((uint32_t*)rmd)[1] &= ~0x80000000;
        pcnetPhysWrite(pDevIns, pThis, addr+7, (uint8_t*)rmd + 7, 1);
    }
    else
    {
        uint32_t rda[3];
        rda[0] = ((uint32_t *)rmd)[2];
        rda[1] = ((uint32_t *)rmd)[1];
        rda[2] = ((uint32_t *)rmd)[0];
        rda[1] |=  0x80000000;
        pcnetPhysWrite(pDevIns, pThis, addr, (void*)&rda[0], sizeof(rda));
        rda[1] &= ~0x80000000;
        pcnetPhysWrite(pDevIns, pThis, addr+7, (uint8_t*)rda + 7, 1);
    }
}

#ifdef IN_RING3
/**
 * Read+Write a TX/RX descriptor to prevent PDMDevHlpPCIPhysWrite() allocating
 * pages later when we shouldn't schedule to EMT. Temporarily hack.
 */
static void pcnetDescTouch(PPDMDEVINS pDevIns, PPCNETSTATE pThis, RTGCPHYS32 addr)
{
    uint8_t aBuf[16];
    size_t cbDesc;
    if (RT_UNLIKELY(BCR_SWSTYLE(pThis) == 0))
        cbDesc = 8;
    else
        cbDesc = 16;
    pcnetPhysRead(pDevIns, pThis, addr, aBuf, cbDesc);
    pcnetPhysWrite(pDevIns, pThis, addr, aBuf, cbDesc);
}
#endif /* IN_RING3 */

/** Checks if it's a bad (as in invalid) RMD.*/
#define IS_RMD_BAD(rmd)      ((rmd).rmd1.ones != 15)

/** The network card is the owner of the RDTE/TDTE, actually it is this driver */
#define CARD_IS_OWNER(desc)   (((desc) & 0x8000))

/** The host is the owner of the RDTE/TDTE -- actually the VM guest. */
#define HOST_IS_OWNER(desc)  (!((desc) & 0x8000))

#ifndef ETHER_IS_MULTICAST /* Net/Open BSD macro it seems */
#define ETHER_IS_MULTICAST(a) ((*(uint8_t *)(a)) & 1)
#endif

#define ETHER_ADDR_LEN ETH_ALEN
#define ETH_ALEN 6
#pragma pack(1)
struct ether_header /** @todo Use RTNETETHERHDR */
{
    uint8_t  ether_dhost[ETH_ALEN]; /**< destination ethernet address */
    uint8_t  ether_shost[ETH_ALEN]; /**< source ethernet address */
    uint16_t ether_type;            /**< packet type ID field */
};
#pragma pack()

#define PRINT_PKTHDR(BUF) do { \
    struct ether_header *hdr = (struct ether_header *)(BUF); \
    Log12(("#%d packet dhost=%02x:%02x:%02x:%02x:%02x:%02x, shost=%02x:%02x:%02x:%02x:%02x:%02x, type=%#06x (bcast=%d)\n", \
           PCNET_INST_NR, \
           hdr->ether_dhost[0],hdr->ether_dhost[1],hdr->ether_dhost[2], \
           hdr->ether_dhost[3],hdr->ether_dhost[4],hdr->ether_dhost[5], \
           hdr->ether_shost[0],hdr->ether_shost[1],hdr->ether_shost[2], \
           hdr->ether_shost[3],hdr->ether_shost[4],hdr->ether_shost[5], \
           htons(hdr->ether_type),                                      \
           !!ETHER_IS_MULTICAST(hdr->ether_dhost)));                    \
} while (0)


#define MULTICAST_FILTER_LEN 8

DECLINLINE(uint32_t) lnc_mchash(const uint8_t *ether_addr)
{
#define LNC_POLYNOMIAL          0xEDB88320UL
    uint32_t crc = 0xFFFFFFFF;
    int idx, bit;
    uint8_t data;

    for (idx = 0; idx < ETHER_ADDR_LEN; idx++)
    {
        for (data = *ether_addr++, bit = 0; bit < MULTICAST_FILTER_LEN; bit++)
        {
            crc = (crc >> 1) ^ (((crc ^ data) & 1) ? LNC_POLYNOMIAL : 0);
            data >>= 1;
        }
    }
    return crc;
#undef LNC_POLYNOMIAL
}

#define CRC(crc, ch)  (crc = (crc >> 8) ^ crctab[(crc ^ (ch)) & 0xff])

/* generated using the AUTODIN II polynomial
 *   x^32 + x^26 + x^23 + x^22 + x^16 +
 *   x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + 1
 */
static const uint32_t crctab[256] =
{
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};

DECLINLINE(int) padr_match(PPCNETSTATE pThis, const uint8_t *buf, size_t size)
{
    struct ether_header *hdr = (struct ether_header *)buf;
    int     result;
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    result = !CSR_DRCVPA(pThis) && !memcmp(hdr->ether_dhost, pThis->aCSR + 12, 6);
#else
    uint8_t padr[6];
    padr[0] = pThis->aCSR[12] & 0xff;
    padr[1] = pThis->aCSR[12] >> 8;
    padr[2] = pThis->aCSR[13] & 0xff;
    padr[3] = pThis->aCSR[13] >> 8;
    padr[4] = pThis->aCSR[14] & 0xff;
    padr[5] = pThis->aCSR[14] >> 8;
    result = !CSR_DRCVPA(pThis) && !memcmp(hdr->ether_dhost, padr, 6);
#endif

    Log11(("#%d packet dhost=%02x:%02x:%02x:%02x:%02x:%02x, padr=%02x:%02x:%02x:%02x:%02x:%02x => %d\n", PCNET_INST_NR,
           hdr->ether_dhost[0],hdr->ether_dhost[1],hdr->ether_dhost[2],
           hdr->ether_dhost[3],hdr->ether_dhost[4],hdr->ether_dhost[5],
           pThis->aCSR[12] & 0xff, pThis->aCSR[12] >> 8,
           pThis->aCSR[13] & 0xff, pThis->aCSR[13] >> 8,
           pThis->aCSR[14] & 0xff, pThis->aCSR[14] >> 8, result));
    RT_NOREF_PV(size);
    return result;
}

DECLINLINE(int) padr_bcast(PPCNETSTATE pThis, const uint8_t *buf, size_t size)
{
    static uint8_t aBCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    struct ether_header *hdr = (struct ether_header *)buf;
    int result = !CSR_DRCVBC(pThis) && !memcmp(hdr->ether_dhost, aBCAST, 6);
    Log11(("#%d padr_bcast result=%d\n", PCNET_INST_NR, result));
    RT_NOREF_PV(size);
   return result;
}

static int ladr_match(PPCNETSTATE pThis, const uint8_t *buf, size_t size)
{
    struct ether_header *hdr = (struct ether_header *)buf;
    if (RT_UNLIKELY(hdr->ether_dhost[0] & 0x01) && ((uint64_t *)&pThis->aCSR[8])[0] != 0LL)
    {
        int index;
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        index = lnc_mchash(hdr->ether_dhost) >> 26;
        return ((uint8_t*)(pThis->aCSR + 8))[index >> 3] & (1 << (index & 7));
#else
        uint8_t ladr[8];
        ladr[0] = pThis->aCSR[8] & 0xff;
        ladr[1] = pThis->aCSR[8] >> 8;
        ladr[2] = pThis->aCSR[9] & 0xff;
        ladr[3] = pThis->aCSR[9] >> 8;
        ladr[4] = pThis->aCSR[10] & 0xff;
        ladr[5] = pThis->aCSR[10] >> 8;
        ladr[6] = pThis->aCSR[11] & 0xff;
        ladr[7] = pThis->aCSR[11] >> 8;
        index = lnc_mchash(hdr->ether_dhost) >> 26;
        return (ladr[index >> 3] & (1 << (index & 7)));
#endif
    }
    RT_NOREF_PV(size);
    return 0;
}


/**
 * Get the receive descriptor ring address with a given index.
 */
DECLINLINE(RTGCPHYS32) pcnetRdraAddr(PPCNETSTATE pThis, int idx)
{
    return pThis->GCRDRA + ((CSR_RCVRL(pThis) - idx) << pThis->iLog2DescSize);
}

/**
 * Get the transmit descriptor ring address with a given index.
 */
DECLINLINE(RTGCPHYS32) pcnetTdraAddr(PPCNETSTATE pThis, int idx)
{
    return pThis->GCTDRA + ((CSR_XMTRL(pThis) - idx) << pThis->iLog2DescSize);
}


#undef htonl
#define htonl(x)    ASMByteSwapU32(x)
#undef htons
#define htons(x)    ( (((x) & 0xff00) >> 8) | (((x) & 0x00ff) << 8) )


static void pcnetSoftReset(PPCNETSTATE pThis)
{
    Log(("#%d pcnetSoftReset:\n", PCNET_INST_NR));

    pThis->u32Lnkst = 0x40;
    pThis->GCRDRA   = 0;
    pThis->GCTDRA   = 0;
    pThis->u32RAP   = 0;

    pThis->aCSR[0]   = 0x0004;
    pThis->aCSR[3]   = 0x0000;
    pThis->aCSR[4]   = 0x0115;
    pThis->aCSR[5]   = 0x0000;
    pThis->aCSR[6]   = 0x0000;
    pThis->aCSR[8]   = 0;
    pThis->aCSR[9]   = 0;
    pThis->aCSR[10]  = 0;
    pThis->aCSR[11]  = 0;
    pThis->aCSR[12]  = RT_LE2H_U16(((uint16_t *)&pThis->aPROM[0])[0]);
    pThis->aCSR[13]  = RT_LE2H_U16(((uint16_t *)&pThis->aPROM[0])[1]);
    pThis->aCSR[14]  = RT_LE2H_U16(((uint16_t *)&pThis->aPROM[0])[2]);
    pThis->aCSR[15] &= 0x21c4;
    CSR_RCVRC(pThis) = 1;
    CSR_XMTRC(pThis) = 1;
    CSR_RCVRL(pThis) = 1;
    CSR_XMTRL(pThis) = 1;
    pThis->aCSR[80]  = 0x1410;
    switch (pThis->uDevType)
    {
    default:
    case DEV_AM79C970A:
        pThis->aCSR[88] = CSR_VERSION_LOW_79C970A;
        pThis->aCSR[89] = CSR_VERSION_HIGH;
        break;
    case DEV_AM79C973:
        pThis->aCSR[88] = CSR_VERSION_LOW_79C973;
        pThis->aCSR[89] = CSR_VERSION_HIGH;
        break;
    case DEV_AM79C960:
    case DEV_AM79C960_EB:
        pThis->aCSR[88] = CSR_VERSION_LOW_79C960;
        pThis->aCSR[89] = 0x0000;
        break;
    }
    pThis->aCSR[94]  = 0x0000;
    pThis->aCSR[100] = 0x0200;
    pThis->aCSR[103] = 0x0105;
    CSR_MISSC(pThis) = 0;
    pThis->aCSR[114] = 0x0000;
    pThis->aCSR[122] = 0x0000;
    pThis->aCSR[124] = 0x0000;
}

/**
 * Check if we have to send an interrupt to the guest. An interrupt can occur on
 * - csr0 (written quite often)
 * - csr4 (only written by pcnetSoftReset(), pcnetStop() or by the guest driver)
 * - csr5 (only written by pcnetSoftReset(), pcnetStop or by the driver guest)
 */
static void pcnetUpdateIrq(PPDMDEVINS pDevIns, PPCNETSTATE pThis)
{
    int      iISR = 0;
    uint16_t csr0 = pThis->aCSR[0];

    csr0 &= ~0x0080; /* clear INTR */

    STAM_PROFILE_ADV_START(&pThis->StatInterrupt, a);

    /* Linux guests set csr4=0x0915
     * W2k   guests set csr3=0x4940 (disable BABL, MERR, IDON, DXSUFLO */

#if 1
    if (    ( (csr0               & ~pThis->aCSR[3]) & 0x5f00)
         || (((pThis->aCSR[4]>>1) & ~pThis->aCSR[4]) & 0x0115)
         || (((pThis->aCSR[5]>>1) &  pThis->aCSR[5]) & 0x0048))
#else
    if (  ( !(pThis->aCSR[3] & 0x4000) && !!(csr0           & 0x4000)) /* BABL */
        ||( !(pThis->aCSR[3] & 0x1000) && !!(csr0           & 0x1000)) /* MISS */
        ||( !(pThis->aCSR[3] & 0x0100) && !!(csr0           & 0x0100)) /* IDON */
        ||( !(pThis->aCSR[3] & 0x0200) && !!(csr0           & 0x0200)) /* TINT */
        ||( !(pThis->aCSR[3] & 0x0400) && !!(csr0           & 0x0400)) /* RINT */
        ||( !(pThis->aCSR[3] & 0x0800) && !!(csr0           & 0x0800)) /* MERR */
        ||( !(pThis->aCSR[4] & 0x0001) && !!(pThis->aCSR[4] & 0x0002)) /* JAB */
        ||( !(pThis->aCSR[4] & 0x0004) && !!(pThis->aCSR[4] & 0x0008)) /* TXSTRT */
        ||( !(pThis->aCSR[4] & 0x0010) && !!(pThis->aCSR[4] & 0x0020)) /* RCVO */
        ||( !(pThis->aCSR[4] & 0x0100) && !!(pThis->aCSR[4] & 0x0200)) /* MFCO */
        ||(!!(pThis->aCSR[5] & 0x0040) && !!(pThis->aCSR[5] & 0x0080)) /* EXDINT */
        ||(!!(pThis->aCSR[5] & 0x0008) && !!(pThis->aCSR[5] & 0x0010)) /* MPINT */)
#endif
    {
        iISR = !!(csr0 & 0x0040); /* CSR_INEA */
        csr0 |= 0x0080; /* set INTR */
    }

#ifdef VBOX
    if (pThis->aCSR[4] & 0x0080) /* UINTCMD */
    {
        pThis->aCSR[4] &= ~0x0080; /* clear UINTCMD */
        pThis->aCSR[4] |=  0x0040; /* set UINT */
        Log(("#%d user int\n", PCNET_INST_NR));
    }
    if (pThis->aCSR[4] & csr0 & 0x0040 /* CSR_INEA */)
    {
        csr0 |=  0x0080; /* set INTR */
        iISR = 1;
    }
#else /* !VBOX */
    if (!!(pThis->aCSR[4] & 0x0080) && CSR_INEA(pThis)) /* UINTCMD */
    {
        pThis->aCSR[4] &= ~0x0080;
        pThis->aCSR[4] |=  0x0040; /* set UINT */
        csr0           |=  0x0080; /* set INTR */
        iISR = 1;
        Log(("#%d user int\n", PCNET_INST_NR));
    }
#endif /* !VBOX */

#if 1
    if (((pThis->aCSR[5]>>1) & pThis->aCSR[5]) & 0x0500)
#else
    if (   (!!(pThis->aCSR[5] & 0x0400) && !!(pThis->aCSR[5] & 0x0800)) /* SINT */
         ||(!!(pThis->aCSR[5] & 0x0100) && !!(pThis->aCSR[5] & 0x0200)) /* SLPINT */)
#endif
    {
        iISR = 1;
        csr0 |= 0x0080; /* INTR */
    }

    if ((pThis->aCSR[7] & 0x0C00) == 0x0C00) /* STINT + STINTE */
        iISR = 1;

    pThis->aCSR[0] = csr0;

    Log2(("#%d set irq iISR=%d\n", PCNET_INST_NR, iISR));

    /* normal path is to _not_ change the IRQ status */
    if (RT_UNLIKELY(iISR != pThis->iISR))
    {
        if (!PCNET_IS_ISA(pThis))
        {
            Log(("#%d INTA=%d\n", PCNET_INST_NR, iISR));
            PDMDevHlpPCISetIrq(pDevIns, 0, iISR);
        }
        else
        {
            Log(("#%d IRQ=%d, state=%d\n", PCNET_INST_NR, pThis->uIsaIrq, iISR));
            PDMDevHlpISASetIrq(pDevIns, pThis->uIsaIrq, iISR);
        }
        pThis->iISR = iISR;
    }
    STAM_PROFILE_ADV_STOP(&pThis->StatInterrupt, a);
}

#ifdef IN_RING3

static void pcnetR3Init(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC)
{
    Log(("#%d pcnetR3Init: init_addr=%#010x\n", PCNET_INST_NR, PHYSADDR(pThis, CSR_IADR(pThis))));

    /* If intialization was invoked with PCI bus mastering disabled, it's not going to
     * go very well. Better report an error.
     */
    if (PCNET_IS_PCI(pThis))
    {
        PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
        uint8_t uCmd = PDMPciDevGetByte(pPciDev, 0x04);

        if (!(uCmd & 4))
        {
            pThis->aCSR[0] |=  0x0801;       /* Set the MERR bit instead of IDON. */
            LogRel(("PCnet#%d: Warning: Initialization failed due to disabled PCI bus mastering.\n", PCNET_INST_NR));
            return;
        }
    }

    /** @todo Documentation says that RCVRL and XMTRL are stored as two's complement!
     *        Software is allowed to write these registers directly. */
# define PCNET_INIT() do { \
            pcnetPhysRead(pDevIns, pThis, PHYSADDR(pThis, CSR_IADR(pThis)),      \
                          (uint8_t *)&initblk, sizeof(initblk));                 \
            pThis->aCSR[15]  = RT_LE2H_U16(initblk.mode);                        \
            CSR_RCVRL(pThis) = (initblk.rlen < 9) ? (1 << initblk.rlen) : 512;   \
            CSR_XMTRL(pThis) = (initblk.tlen < 9) ? (1 << initblk.tlen) : 512;   \
            pThis->aCSR[ 6]  = (initblk.tlen << 12) | (initblk.rlen << 8);       \
            pThis->aCSR[ 8]  = RT_LE2H_U16(initblk.ladrf1);                      \
            pThis->aCSR[ 9]  = RT_LE2H_U16(initblk.ladrf2);                      \
            pThis->aCSR[10]  = RT_LE2H_U16(initblk.ladrf3);                      \
            pThis->aCSR[11]  = RT_LE2H_U16(initblk.ladrf4);                      \
            pThis->aCSR[12]  = RT_LE2H_U16(initblk.padr1);                       \
            pThis->aCSR[13]  = RT_LE2H_U16(initblk.padr2);                       \
            pThis->aCSR[14]  = RT_LE2H_U16(initblk.padr3);                       \
            pThis->GCRDRA    = PHYSADDR(pThis, initblk.rdra);                    \
            pThis->GCTDRA    = PHYSADDR(pThis, initblk.tdra);                    \
        } while (0)

    if (BCR_SSIZE32(pThis))
    {
        struct INITBLK32 initblk;
        pThis->GCUpperPhys = 0;
        PCNET_INIT();
        Log(("#%d initblk.rlen=%#04x, initblk.tlen=%#04x\n",
             PCNET_INST_NR, initblk.rlen, initblk.tlen));
    }
    else
    {
        struct INITBLK16 initblk;
        pThis->GCUpperPhys = (0xff00 & (uint32_t)pThis->aCSR[2]) << 16;
        PCNET_INIT();
        Log(("#%d initblk.rlen=%#04x, initblk.tlen=%#04x\n",
             PCNET_INST_NR, initblk.rlen, initblk.tlen));
    }

# undef PCNET_INIT

    size_t cbRxBuffers = 0;
    for (int i = CSR_RCVRL(pThis); i >= 1; i--)
    {
        RMD rmd;
        RTGCPHYS32 rdaddr = PHYSADDR(pThis, pcnetRdraAddr(pThis, i));

        pcnetDescTouch(pDevIns,  pThis, rdaddr);
        /* At this time it is not guaranteed that the buffers are already initialized. */
        if (pcnetRmdLoad(pDevIns, pThis, &rmd, rdaddr, false))
        {
            uint32_t cbBuf = 4096U-rmd.rmd1.bcnt;
            cbRxBuffers += cbBuf;
        }
    }

    for (int i = CSR_XMTRL(pThis); i >= 1; i--)
    {
        RTGCPHYS32 tdaddr = PHYSADDR(pThis, pcnetTdraAddr(pThis, i));

        pcnetDescTouch(pDevIns, pThis, tdaddr);
    }

    /*
     * Heuristics: The Solaris pcn driver allocates too few RX buffers (128 buffers of a
     * size of 128 bytes are 16KB in summary) leading to frequent RX buffer overflows. In
     * that case we don't signal RX overflows through the CSR0_MISS flag as the driver
     * re-initializes the device on every miss. Other guests use at least 32 buffers of
     * usually 1536 bytes and should therefore not run into condition. If they are still
     * short in RX buffers we notify this condition.
     */
    pThis->fSignalRxMiss = (cbRxBuffers == 0 || cbRxBuffers >= 32*_1K);

    if (pThisCC->pDrv)
        pThisCC->pDrv->pfnSetPromiscuousMode(pThisCC->pDrv, CSR_PROM(pThis));

    CSR_RCVRC(pThis) = CSR_RCVRL(pThis);
    CSR_XMTRC(pThis) = CSR_XMTRL(pThis);

    /* Reset cached RX and TX states */
    CSR_CRST(pThis) = CSR_CRBC(pThis) = CSR_NRST(pThis) = CSR_NRBC(pThis) = 0;
    CSR_CXST(pThis) = CSR_CXBC(pThis) = CSR_NXST(pThis) = CSR_NXBC(pThis) = 0;

    LogRel(("PCnet#%d: Init: SWSTYLE=%d GCRDRA=%#010x[%d] GCTDRA=%#010x[%d]%s\n",
            PCNET_INST_NR, BCR_SWSTYLE(pThis),
            pThis->GCRDRA, CSR_RCVRL(pThis), pThis->GCTDRA, CSR_XMTRL(pThis),
            !pThis->fSignalRxMiss ? " (CSR0_MISS disabled)" : ""));

    if (pThis->GCRDRA & (pThis->iLog2DescSize - 1))
        LogRel(("PCnet#%d: Warning: Misaligned RDRA\n", PCNET_INST_NR));
    if (pThis->GCTDRA & (pThis->iLog2DescSize - 1))
        LogRel(("PCnet#%d: Warning: Misaligned TDRA\n", PCNET_INST_NR));

    pThis->aCSR[0] |=  0x0101;       /* Initialization done */
    pThis->aCSR[0] &= ~0x0004;       /* clear STOP bit */
}

#endif /* IN_RING3 */

/**
 * Start RX/TX operation.
 */
static void pcnetStart(PPDMDEVINS pDevIns, PPCNETSTATE pThis)
{
    Log(("#%d pcnetStart:\n", PCNET_INST_NR));

    /* Reset any cached RX/TX descriptor state. */
    CSR_CRDA(pThis) = CSR_CRBA(pThis) = CSR_NRDA(pThis) = CSR_NRBA(pThis) = 0;
    CSR_CRBC(pThis) = CSR_NRBC(pThis) = CSR_CRST(pThis) = 0;

    if (!CSR_DTX(pThis))
        pThis->aCSR[0] |= 0x0010;    /* set TXON */
    if (!CSR_DRX(pThis))
        pThis->aCSR[0] |= 0x0020;    /* set RXON */
    pThis->aCSR[0] &= ~0x0004;       /* clear STOP bit */
    pThis->aCSR[0] |=  0x0002;       /* STRT */

    pcnetPollTimerStart(pDevIns, pThis); /* start timer if it was stopped */
}

/**
 * Stop RX/TX operation.
 */
static void pcnetStop(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC)
{
    Log(("#%d pcnetStop:\n", PCNET_INST_NR));
    pThis->aCSR[0]  =  0x0004;
    pThis->aCSR[4] &= ~0x02c2;
    pThis->aCSR[5] &= ~0x0011;
    pcnetPollTimer(pDevIns, pThis, pThisCC);
}

/**
 * Wakes up a receive thread stuck waiting for buffers.
 */
static void pcnetWakeupReceive(PPDMDEVINS pDevIns)
{
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    STAM_COUNTER_INC(&pThis->StatRxOverflowWakeup);
    if (pThis->hEventOutOfRxSpace != NIL_SUPSEMEVENT)
    {
        int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEventOutOfRxSpace);
        AssertRC(rc);
    }
}

/**
 * Poll Receive Descriptor Table Entry and cache the results in the appropriate registers.
 *
 * @note    Once a descriptor belongs to the network card (this driver), it
 *          cannot be changed by the host (the guest driver) anymore. Well, it
 *          could but the results are undefined by definition.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared PCnet state data.
 * @param   fSkipCurrent    if true, don't scan the current RDTE.
 */
static void pcnetRdtePoll(PPDMDEVINS pDevIns, PPCNETSTATE pThis, bool fSkipCurrent=false)
{
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatRdtePoll), a);
    /* assume lack of a next receive descriptor */
    CSR_NRST(pThis) = 0;

    if (RT_LIKELY(pThis->GCRDRA))
    {
        /*
         * The current receive message descriptor.
         */
        RMD        rmd;
        int        i = CSR_RCVRC(pThis);
        RTGCPHYS32 addr;

        if (i < 1)
            i = CSR_RCVRL(pThis);

        if (!fSkipCurrent)
        {
            addr = pcnetRdraAddr(pThis, i);
            CSR_CRDA(pThis) = CSR_CRBA(pThis) = 0;
            CSR_CRBC(pThis) = CSR_CRST(pThis) = 0;
            if (!pcnetRmdLoad(pDevIns, pThis, &rmd, PHYSADDR(pThis, addr), true))
            {
                STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatRdtePoll), a);
                return;
            }
            if (RT_LIKELY(!IS_RMD_BAD(rmd)))
            {
                CSR_CRDA(pThis) = addr;                        /* Receive Descriptor Address */
                CSR_CRBA(pThis) = rmd.rmd0.rbadr;              /* Receive Buffer Address */
                CSR_CRBC(pThis) = rmd.rmd1.bcnt;               /* Receive Byte Count */
                CSR_CRST(pThis) = ((uint32_t *)&rmd)[1] >> 16; /* Receive Status */
                if (pThis->fMaybeOutOfSpace)
                    pcnetWakeupReceive(pDevIns);
            }
            else
            {
                STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatRdtePoll), a);
                /* This is not problematic since we don't own the descriptor
                 * We actually do own it, otherwise pcnetRmdLoad would have returned false.
                 * Don't flood the release log with errors.
                 */
                if (++pThis->uCntBadRMD < 50)
                    LogRel(("PCnet#%d: BAD RMD ENTRIES AT %#010x (i=%d)\n",
                            PCNET_INST_NR, addr, i));
                return;
            }
        }

        /*
         * The next descriptor.
         */
        if (--i < 1)
            i = CSR_RCVRL(pThis);
        addr = pcnetRdraAddr(pThis, i);
        CSR_NRDA(pThis) = CSR_NRBA(pThis) = 0;
        CSR_NRBC(pThis) = 0;
        if (!pcnetRmdLoad(pDevIns, pThis, &rmd, PHYSADDR(pThis, addr), true))
        {
            STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatRdtePoll), a);
            return;
        }
        if (RT_LIKELY(!IS_RMD_BAD(rmd)))
        {
            CSR_NRDA(pThis) = addr;                         /* Receive Descriptor Address */
            CSR_NRBA(pThis) = rmd.rmd0.rbadr;               /* Receive Buffer Address */
            CSR_NRBC(pThis) = rmd.rmd1.bcnt;                /* Receive Byte Count */
            CSR_NRST(pThis) = ((uint32_t *)&rmd)[1] >> 16;  /* Receive Status */
        }
        else
        {
            STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatRdtePoll), a);
            /* This is not problematic since we don't own the descriptor
             * We actually do own it, otherwise pcnetRmdLoad would have returned false.
             * Don't flood the release log with errors.
             */
            if (++pThis->uCntBadRMD < 50)
                LogRel(("PCnet#%d: BAD RMD ENTRIES + AT %#010x (i=%d)\n",
                        PCNET_INST_NR, addr, i));
            return;
        }

        /**
         * @todo NNRD
         */
    }
    else
    {
        CSR_CRDA(pThis) = CSR_CRBA(pThis) = CSR_NRDA(pThis) = CSR_NRBA(pThis) = 0;
        CSR_CRBC(pThis) = CSR_NRBC(pThis) = CSR_CRST(pThis) = 0;
    }
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatRdtePoll), a);
}

/**
 * Poll Transmit Descriptor Table Entry
 * @return true if transmit descriptors available
 */
static int pcnetTdtePoll(PPDMDEVINS pDevIns, PPCNETSTATE pThis, TMD *tmd)
{
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatTdtePoll), a);
    if (RT_LIKELY(pThis->GCTDRA))
    {
        RTGCPHYS32 cxda = pcnetTdraAddr(pThis, CSR_XMTRC(pThis));

        if (!pcnetTmdTryLoad(pDevIns, pThis, tmd, PHYSADDR(pThis, cxda)))
        {
            STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTdtePoll), a);
            return 0;
        }

        if (RT_UNLIKELY(tmd->tmd1.ones != 15))
        {
            STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTdtePoll), a);
            LogRel(("PCnet#%d: BAD TMD XDA=%#010x\n",
                    PCNET_INST_NR, PHYSADDR(pThis, cxda)));
            return 0;
        }

        /* previous xmit descriptor */
        CSR_PXDA(pThis) = CSR_CXDA(pThis);
        CSR_PXBC(pThis) = CSR_CXBC(pThis);
        CSR_PXST(pThis) = CSR_CXST(pThis);

        /* set current transmit descriptor. */
        CSR_CXDA(pThis) = cxda;
        CSR_CXBC(pThis) = tmd->tmd1.bcnt;
        CSR_CXST(pThis) = ((uint32_t *)tmd)[1] >> 16;
        STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTdtePoll), a);
        return CARD_IS_OWNER(CSR_CXST(pThis));
    }
    else
    {
        /** @todo consistency with previous receive descriptor */
        CSR_CXDA(pThis) = 0;
        CSR_CXBC(pThis) = CSR_CXST(pThis) = 0;
        STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTdtePoll), a);
        return 0;
    }
}


/**
 * Write data into guest receive buffers.
 */
static void pcnetReceiveNoSync(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC,
                               const uint8_t *buf, size_t cbToRecv, bool fAddFCS, bool fLoopback)
{
    int is_padr  = 0;
    int is_bcast = 0;
    int is_ladr  = 0;
    unsigned iRxDesc;
    int cbPacket;

    if (RT_UNLIKELY(CSR_DRX(pThis) || CSR_STOP(pThis) || CSR_SPND(pThis) || !cbToRecv))
        return;

    /*
     * Drop packets if the VM is not running yet/anymore.
     */
    VMSTATE enmVMState = PDMDevHlpVMState(pDevIns);
    if (    enmVMState != VMSTATE_RUNNING
        &&  enmVMState != VMSTATE_RUNNING_LS)
        return;

    /*
     * Drop packets if the cable is not connected
     */
    if (!pcnetIsLinkUp(pThis))
        return;

    Log(("#%d pcnetReceiveNoSync: size=%d\n", PCNET_INST_NR, cbToRecv));

    /*
     * Perform address matching.
     */
    if (   CSR_PROM(pThis)
        || (is_padr  = padr_match(pThis, buf, cbToRecv))
        || (is_bcast = padr_bcast(pThis, buf, cbToRecv))
        || (is_ladr  = ladr_match(pThis, buf, cbToRecv)))
    {
        if (HOST_IS_OWNER(CSR_CRST(pThis)))
            pcnetRdtePoll(pDevIns, pThis);
        if (RT_UNLIKELY(HOST_IS_OWNER(CSR_CRST(pThis))))
        {
            /* Not owned by controller. This should not be possible as
             * we already called pcnetR3CanReceive(). */
            LogRel(("PCnet#%d: no buffer: RCVRC=%d\n", PCNET_INST_NR, CSR_RCVRC(pThis)));
            /* Dump the status of all RX descriptors */
            const unsigned  cb = 1 << pThis->iLog2DescSize;
            RTGCPHYS32      GCPhys = pThis->GCRDRA;
            iRxDesc = CSR_RCVRL(pThis);
            while (iRxDesc-- > 0)
            {
                RMD rmd;
                pcnetRmdLoad(pDevIns, pThis, &rmd, PHYSADDR(pThis, GCPhys), false);
                LogRel(("  %#010x\n", rmd.rmd1));
                GCPhys += cb;
            }
            pThis->aCSR[0] |= 0x1000; /* Set MISS flag */
            CSR_MISSC(pThis)++;
        }
        else
        {
            PCRTNETETHERHDR pEth = (PCRTNETETHERHDR)buf;
            bool fStrip = false;
            size_t len_802_3;
            uint8_t   *src = &pThis->abRecvBuf[8];
            RTGCPHYS32 crda = CSR_CRDA(pThis);
            RTGCPHYS32 next_crda;
            RMD        rmd, next_rmd;

            /*
             * Ethernet framing considers these two octets to be
             * payload type; 802.3 framing considers them to be
             * payload length.  IEEE 802.3x-1997 restricts Ethernet
             * type to be greater than or equal to 1536 (0x0600), so
             * that both framings can coexist on the wire.
             *
             * NB: CSR_ASTRP_RCV bit affects only 802.3 frames!
             */
            len_802_3 = RT_BE2H_U16(pEth->EtherType);
            if (len_802_3 < 46 && CSR_ASTRP_RCV(pThis))
            {
                cbToRecv = RT_MIN(sizeof(RTNETETHERHDR) + len_802_3, cbToRecv);
                fStrip = true;
                fAddFCS = false;
            }

            memcpy(src, buf, cbToRecv);

            if (!fStrip) {
                /* In loopback mode, Runt Packed Accept is always enabled internally;
                 * don't do any padding because guest may be looping back very short packets.
                 */
                if (!fLoopback)
                    while (cbToRecv < 60)
                        src[cbToRecv++] = 0;

                if (fAddFCS)
                {
                    uint32_t fcs = UINT32_MAX;
                    uint8_t *p = src;

                    while (p != &src[cbToRecv])
                        CRC(fcs, *p++);

                    /* FCS at the end of the packet */
                    ((uint32_t *)&src[cbToRecv])[0] = htonl(fcs);
                    cbToRecv += 4;
                }
            }

            cbPacket = (int)cbToRecv;                           Assert((size_t)cbPacket == cbToRecv);

#ifdef LOG_ENABLED
            PRINT_PKTHDR(buf);
#endif

            pcnetRmdLoad(pDevIns, pThis, &rmd, PHYSADDR(pThis, crda), false);
            /*if (!CSR_LAPPEN(pThis))*/
                rmd.rmd1.stp = 1;

            size_t cbBuf = RT_MIN(4096 - (size_t)rmd.rmd1.bcnt, cbToRecv);
            RTGCPHYS32 rbadr = PHYSADDR(pThis, rmd.rmd0.rbadr);

            /* save the old value to check if it was changed as long as we didn't
             * hold the critical section */
            iRxDesc = CSR_RCVRC(pThis);

            /* We have to leave the critical section here or we risk deadlocking
             * with EMT when the write is to an unallocated page or has an access
             * handler associated with it.
             *
             * This shouldn't be a problem because:
             *  - any modification to the RX descriptor by the driver is
             *    forbidden as long as it is owned by the device
             *  - we don't cache any register state beyond this point
             */
            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
            pcnetPhysWrite(pDevIns, pThis, rbadr, src, cbBuf);
            int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
            PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rc);

            /* RX disabled in the meantime? If so, abort RX. */
            if (RT_UNLIKELY(CSR_DRX(pThis) || CSR_STOP(pThis) || CSR_SPND(pThis)))
                return;

            /* Was the register modified in the meantime? If so, don't touch the
             * register but still update the RX descriptor. */
            if (RT_LIKELY(iRxDesc == CSR_RCVRC(pThis)))
            {
                if (iRxDesc-- < 2)
                    iRxDesc = CSR_RCVRL(pThis);
                CSR_RCVRC(pThis) = iRxDesc;
            }
            else
                iRxDesc = CSR_RCVRC(pThis);

            src      += cbBuf;
            cbToRecv -= cbBuf;

            while (cbToRecv > 0)
            {
                /* Read the entire next descriptor as we're likely to need it. */
                next_crda = pcnetRdraAddr(pThis, iRxDesc);

                /* Check next descriptor's own bit. If we don't own it, we have
                 * to quit and write error status into the last descriptor we own.
                 */
                if (!pcnetRmdLoad(pDevIns, pThis, &next_rmd, PHYSADDR(pThis, next_crda), true))
                    break;

                /* Write back current descriptor, clear the own bit. */
                pcnetRmdStorePassHost(pDevIns, pThis, &rmd, PHYSADDR(pThis, crda));

                /* Switch to the next descriptor */
                crda = next_crda;
                rmd  = next_rmd;

                cbBuf = RT_MIN(4096 - (size_t)rmd.rmd1.bcnt, cbToRecv);
                RTGCPHYS32 rbadr2 = PHYSADDR(pThis, rmd.rmd0.rbadr);

                /* We have to leave the critical section here or we risk deadlocking
                 * with EMT when the write is to an unallocated page or has an access
                 * handler associated with it. See above for additional comments. */
                PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
                pcnetPhysWrite(pDevIns, pThis, rbadr2, src, cbBuf);
                rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
                PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rc);

                /* RX disabled in the meantime? If so, abort RX. */
                if (RT_UNLIKELY(CSR_DRX(pThis) || CSR_STOP(pThis) || CSR_SPND(pThis)))
                    return;

                /* Was the register modified in the meantime? If so, don't touch the
                 * register but still update the RX descriptor. */
                if (RT_LIKELY(iRxDesc == CSR_RCVRC(pThis)))
                {
                    if (iRxDesc-- < 2)
                        iRxDesc = CSR_RCVRL(pThis);
                    CSR_RCVRC(pThis) = iRxDesc;
                }
                else
                    iRxDesc = CSR_RCVRC(pThis);

                src      += cbBuf;
                cbToRecv -= cbBuf;
            }

            if (RT_LIKELY(cbToRecv == 0))
            {
                rmd.rmd1.enp  = 1;
                rmd.rmd1.pam  = !CSR_PROM(pThis) && is_padr;
                rmd.rmd1.lafm = !CSR_PROM(pThis) && is_ladr;
                rmd.rmd1.bam  = !CSR_PROM(pThis) && is_bcast;
                rmd.rmd2.mcnt = cbPacket;
                rmd.rmd2.zeros = 0;

                STAM_REL_COUNTER_ADD(&pThis->StatReceiveBytes, cbPacket);
            }
            else
            {
                Log(("#%d: Overflow by %ubytes\n", PCNET_INST_NR, cbToRecv));
                rmd.rmd1.oflo = 1;
                rmd.rmd1.buff = 1;
                rmd.rmd1.err  = 1;
            }

            /* write back, clear the own bit */
            pcnetRmdStorePassHost(pDevIns, pThis, &rmd, PHYSADDR(pThis, crda));

            pThis->aCSR[0] |= 0x0400;

            Log(("#%d RCVRC=%d CRDA=%#010x\n", PCNET_INST_NR, CSR_RCVRC(pThis), PHYSADDR(pThis, CSR_CRDA(pThis))));
#ifdef LOG_ENABLED
            PRINT_RMD(&rmd);
#endif

            /* guest driver is owner: force repoll of current and next RDTEs */
            CSR_CRST(pThis) = 0;
        }
    }

    /* see description of TXDPOLL:
     * ``transmit polling will take place following receive activities'' */
    if (!fLoopback)
        pcnetPollRxTx(pDevIns, pThis, pThisCC);
    pcnetUpdateIrq(pDevIns, pThis);
}


#ifdef IN_RING3
/**
 * @callback_method_impl{FNPDMTASKDEV,
 * This is just a very simple way of delaying sending to R3.}
 */
static DECLCALLBACK(void) pcnetR3XmitTaskCallback(PPDMDEVINS pDevIns, void *pvUser)
{
    PPCNETSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    NOREF(pvUser);

    /*
     * Transmit as much as we can.
     */
    pcnetXmitPending(pDevIns, pThis, pThisCC, true /*fOnWorkerThread*/);
}
#endif /* IN_RING3 */


/**
 * Allocates a scatter/gather buffer for a transfer.
 *
 * @returns See PPDMINETWORKUP::pfnAllocBuf.
 * @param   pThis       The shared PCNet state data.
 * @param   cbMin       The minimum buffer size.
 * @param   fLoopback   Set if we're in loopback mode.
 * @param   pSgLoop     Pointer to stack storage for the loopback SG.
 * @param   ppSgBuf     Where to return the SG buffer descriptor on success.
 *                      Always set.
 */
DECLINLINE(int) pcnetXmitAllocBuf(PPCNETSTATE pThis, PPCNETSTATECC pThisCC, size_t cbMin, bool fLoopback,
                                  PPDMSCATTERGATHER pSgLoop, PPPDMSCATTERGATHER ppSgBuf)
{
    int rc;

    if (RT_UNLIKELY(fLoopback)) /* hope that loopback mode is rare */
    {
        pSgLoop->fFlags      = PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1;
        pSgLoop->cbUsed      = 0;
        pSgLoop->cbAvailable = sizeof(pThis->abLoopBuf);
        pSgLoop->pvAllocator = pThis;
        pSgLoop->pvUser      = NULL;
        pSgLoop->cSegs       = 1;
        pSgLoop->aSegs[0].cbSeg = sizeof(pThis->abLoopBuf);
        pSgLoop->aSegs[0].pvSeg = pThis->abLoopBuf;
        *ppSgBuf = pSgLoop;
        rc = VINF_SUCCESS;
    }
    else
    {
        PPDMINETWORKUP pDrv = pThisCC->pDrv;
        if (RT_LIKELY(pDrv))
        {
            rc = pDrv->pfnAllocBuf(pDrv, cbMin, NULL /*pGso*/, ppSgBuf);
            AssertMsg(rc == VINF_SUCCESS || rc == VERR_TRY_AGAIN || rc == VERR_NET_DOWN || rc == VERR_NO_MEMORY, ("%Rrc\n", rc));
            if (RT_FAILURE(rc))
                *ppSgBuf = NULL;
        }
        else
        {
            rc = VERR_NET_DOWN;
            *ppSgBuf = NULL;
        }
    }
    return rc;
}


/**
 * Frees an unsent buffer.
 *
 * @param   pThisCC         The PCNet state data for the current context.
 * @param   fLoopback       Set if we're in loopback mode.
 * @param   pSgBuf          The SG to free.  Can be NULL.
 */
DECLINLINE(void) pcnetXmitFreeBuf(PPCNETSTATECC pThisCC, bool fLoopback, PPDMSCATTERGATHER pSgBuf)
{
    if (pSgBuf)
    {
        if (RT_UNLIKELY(fLoopback))
            pSgBuf->pvAllocator = NULL;
        else
        {
            PPDMINETWORKUP pDrv = pThisCC->pDrv;
            if (RT_LIKELY(pDrv))
                pDrv->pfnFreeBuf(pDrv, pSgBuf);
        }
    }
}


/**
 * Sends the scatter/gather buffer.
 *
 * Wrapper around PDMINETWORKUP::pfnSendBuf, so check it out for the fine print.
 *
 * @returns See PDMINETWORKUP::pfnSendBuf.
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared PCNet state data.
 * @param   pThisCC         The PCNet state data for the current context.
 * @param   fLoopback       Set if we're in loopback mode.
 * @param   pSgBuf          The SG to send.
 * @param   fOnWorkerThread Set if we're being called on a work thread.  Clear
 *                          if an EMT.
 */
DECLINLINE(int) pcnetXmitSendBuf(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, bool fLoopback,
                                 PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    int rc;
    STAM_REL_COUNTER_ADD(&pThis->StatTransmitBytes, pSgBuf->cbUsed);
    if (RT_UNLIKELY(fLoopback)) /* hope that loopback mode is rare */
    {
        Assert(pSgBuf->pvAllocator == (void *)pThis);
        pThis->Led.Asserted.s.fReading = pThis->Led.Actual.s.fReading = 1;
        if (HOST_IS_OWNER(CSR_CRST(pThis)))
            pcnetRdtePoll(pDevIns, pThis);

        pcnetReceiveNoSync(pDevIns, pThis, pThisCC, pThis->abLoopBuf, pSgBuf->cbUsed, true /* fAddFCS */, fLoopback);
        pThis->Led.Actual.s.fReading = 0;
        rc = VINF_SUCCESS;
    }
    else
    {
        /** @todo We used to leave the critsect here, not sure if that's necessary any
         *        longer.  If we could avoid that we could cache a bit more info in
         *        the loop and make it part of the driver<->device contract, saving
         *        critsect mess down in DrvIntNet. */
        STAM_PROFILE_START(&pThis->CTX_SUFF_Z(StatTransmitSend), a);
        if (pSgBuf->cbUsed > 70) /* unqualified guess */
            pThis->Led.Asserted.s.fWriting = pThis->Led.Actual.s.fWriting = 1;

        PPDMINETWORKUP pDrv = pThisCC->pDrv;
        if (RT_LIKELY(pDrv))
        {
            rc = pDrv->pfnSendBuf(pDrv, pSgBuf, fOnWorkerThread);
            AssertMsg(rc == VINF_SUCCESS || rc == VERR_NET_DOWN || rc == VERR_NET_NO_BUFFER_SPACE, ("%Rrc\n", rc));
        }
        else
            rc = VERR_NET_DOWN;

        pThis->Led.Actual.s.fWriting = 0;
        STAM_PROFILE_STOP(&pThis->CTX_SUFF_Z(StatTransmitSend), a);
    }
    return rc;
}


/**
 * pcnetXmitRead1st worker that handles the unlikely + slower segmented code
 * path.
 */
static void pcnetXmitRead1stSlow(PPDMDEVINS pDevIns, RTGCPHYS32 GCPhysFrame, unsigned cbFrame, PPDMSCATTERGATHER pSgBuf)
{
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    pSgBuf->cbUsed = cbFrame;
    for (uint32_t iSeg = 0; ; iSeg++)
    {
        if (iSeg >= pSgBuf->cSegs)
        {
            AssertFailed();
            LogRelMax(10, ("PCnet: pcnetXmitRead1stSlow: segment overflow -> ignoring\n"));
            return;
        }

        uint32_t cbRead = (uint32_t)RT_MIN(cbFrame, pSgBuf->aSegs[iSeg].cbSeg);
        pcnetPhysRead(pDevIns, pThis, GCPhysFrame, pSgBuf->aSegs[iSeg].pvSeg, cbRead);
        cbFrame -= cbRead;
        if (!cbFrame)
            return;
        GCPhysFrame += cbRead;
    }
}


/**
 * pcnetXmitSgReadMore worker that handles the unlikely + slower segmented code
 * path.
 */
static void pcnetXmitReadMoreSlow(PPDMDEVINS pDevIns, RTGCPHYS32 GCPhysFrame, unsigned cbFrame, PPDMSCATTERGATHER pSgBuf)
{
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);

    /* Find the segment which we'll put the next byte into. */
    size_t      off    = pSgBuf->cbUsed;
    size_t      offSeg = 0;
    uint32_t    iSeg   = 0;
    while (offSeg + pSgBuf->aSegs[iSeg].cbSeg <= off)
    {
        offSeg += pSgBuf->aSegs[iSeg].cbSeg;
        iSeg++;
        if (iSeg >= pSgBuf->cSegs)
        {
            AssertFailed();
            LogRelMax(10, ("PCnet: pcnetXmitReadMoreSlow: segment overflow #1 -> ignoring\n"));
            return;
        }
    }

    /* Commit before we start copying so we can decrement cbFrame. */
    pSgBuf->cbUsed = off + cbFrame;

    /* Deal with the first segment if we at an offset into it. */
    if (off != offSeg)
    {
        size_t   offIntoSeg = off - offSeg;
        uint32_t cbRead     = (uint32_t)RT_MIN(pSgBuf->aSegs[iSeg].cbSeg - offIntoSeg, cbFrame);
        pcnetPhysRead(pDevIns, pThis, GCPhysFrame,
                      (uint8_t *)pSgBuf->aSegs[iSeg].pvSeg + offIntoSeg, cbRead);
        cbFrame -= cbRead;
        if (!cbFrame)
            return;
        GCPhysFrame += cbRead;
        iSeg++;
    }

    /* For the remainder, we've got whole segments. */
    for (;; iSeg++)
    {
        if (iSeg >= pSgBuf->cSegs)
        {
            AssertFailed();
            LogRelMax(10, ("PCnet: pcnetXmitReadMoreSlow: segment overflow #2 -> ignoring\n"));
            return;
        }

        uint32_t cbRead = (uint32_t)RT_MIN(pSgBuf->aSegs[iSeg].cbSeg, cbFrame);
        pcnetPhysRead(pDevIns, pThis, GCPhysFrame, pSgBuf->aSegs[iSeg].pvSeg, cbRead);
        cbFrame -= cbRead;
        if (!cbFrame)
            return;
        GCPhysFrame += cbFrame;
    }
}


/**
 * Reads the first part of a frame into the scatter gather buffer.
 */
DECLINLINE(void) pcnetXmitRead1st(PPDMDEVINS pDevIns, PPCNETSTATE pThis, RTGCPHYS32 GCPhysFrame, const unsigned cbFrame,
                                  PPDMSCATTERGATHER pSgBuf)
{
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect)); RT_NOREF(pThis);
    Assert(pSgBuf->cbAvailable >= cbFrame);

    if (RT_LIKELY(pSgBuf->aSegs[0].cbSeg >= cbFrame)) /* justification: all drivers returns a single segment atm. */
    {
        pSgBuf->cbUsed = cbFrame;
        pcnetPhysRead(pDevIns, pThis, GCPhysFrame, pSgBuf->aSegs[0].pvSeg, cbFrame);
    }
    else
        pcnetXmitRead1stSlow(pDevIns, GCPhysFrame, cbFrame, pSgBuf);
}

/**
 * Reads more into the current frame.
 */
DECLINLINE(void) pcnetXmitReadMore(PPDMDEVINS pDevIns, RTGCPHYS32 GCPhysFrame, const unsigned cbFrame, PPDMSCATTERGATHER pSgBuf)
{
    size_t off = pSgBuf->cbUsed;
    Assert(pSgBuf->cbAvailable >= cbFrame + off);

    if (RT_LIKELY(pSgBuf->aSegs[0].cbSeg >= cbFrame + off))
    {
        pSgBuf->cbUsed = cbFrame + off;
        PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
        pcnetPhysRead(pDevIns, pThis, GCPhysFrame, (uint8_t *)pSgBuf->aSegs[0].pvSeg + off, cbFrame);
    }
    else
        pcnetXmitReadMoreSlow(pDevIns, GCPhysFrame, cbFrame, pSgBuf);
}


/**
 * Fails a TMD with a link down error.
 */
static void pcnetXmitFailTMDLinkDown(PPCNETSTATE pThis, TMD *pTmd)
{
    /* make carrier error - hope this is correct. */
    pThis->cLinkDownReported++;
    pTmd->tmd2.lcar = pTmd->tmd1.err = 1;
    pThis->aCSR[0] |= RT_BIT(15) | RT_BIT(13); /* ERR | CERR */
    pThis->Led.Asserted.s.fError = pThis->Led.Actual.s.fError = 1;
    Log(("#%d pcnetTransmit: Signaling send error. swstyle=%#x\n",
         PCNET_INST_NR, pThis->aBCR[BCR_SWS]));
}

/**
 * Fails a TMD with a generic error.
 */
static void pcnetXmitFailTMDGeneric(PPCNETSTATE pThis, TMD *pTmd)
{
    /* make carrier error - hope this is correct. */
    pTmd->tmd2.lcar = pTmd->tmd1.err = 1;
    pThis->aCSR[0] |= RT_BIT(15) | RT_BIT(13); /* ERR | CERR */
    pThis->Led.Asserted.s.fError = pThis->Led.Actual.s.fError = 1;
    Log(("#%d pcnetTransmit: Signaling send error. swstyle=%#x\n",
         PCNET_INST_NR, pThis->aBCR[BCR_SWS]));
}


/**
 * Try to transmit frames
 */
static void pcnetTransmit(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC)
{
    if (RT_UNLIKELY(!CSR_TXON(pThis)))
    {
        pThis->aCSR[0] &= ~0x0008; /* Clear TDMD */
        return;
    }

    /*
     * Check the current transmit descriptors.
     */
    TMD tmd;
    if (!pcnetTdtePoll(pDevIns, pThis, &tmd))
        return;

    /*
     * Clear TDMD.
     */
    pThis->aCSR[0] &= ~0x0008;

    /*
     * Transmit pending packets if possible, defer it if we cannot do it
     * in the current context.
     */
#if defined(IN_RING0) || defined(IN_RC)
    if (!pThisCC->pDrv)
    {
        int rc = PDMDevHlpTaskTrigger(pDevIns, pThis->hXmitTask);
        AssertRC(rc);
    }
    else
#endif
    {
        int rc = pcnetXmitPending(pDevIns, pThis, pThisCC, false /*fOnWorkerThread*/);
        if (rc == VERR_TRY_AGAIN)
            rc = VINF_SUCCESS;
        AssertRC(rc);
    }
}


/**
 * Actually try transmit frames.
 *
 * @threads TX or EMT.
 */
static int pcnetAsyncTransmit(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, bool fOnWorkerThread)
{
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));

    /*
     * Just cleared transmit demand if the transmitter is off.
     */
    if (RT_UNLIKELY(!CSR_TXON(pThis)))
    {
        pThis->aCSR[0] &= ~0x0008; /* Clear TDMD */
        return VINF_SUCCESS;
    }

    /*
     * Iterate the transmit descriptors.
     */
    int         rc;
    unsigned    cFlushIrq = 0;
    int         cMax = 32;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatTransmit), a);
    do
    {
#ifdef VBOX_WITH_STATISTICS
        unsigned cBuffers = 1;
#endif
        TMD tmd;
        if (!pcnetTdtePoll(pDevIns, pThis, &tmd))
            break;

#ifdef LOG_ENABLED
        Log10(("#%d TMDLOAD %#010x\n", PCNET_INST_NR, PHYSADDR(pThis, CSR_CXDA(pThis))));
        PRINT_TMD(&tmd);
#endif
        bool const          fLoopback = CSR_LOOP(pThis);
        PDMSCATTERGATHER    SgLoop;
        PPDMSCATTERGATHER   pSgBuf;

        /*
         * The typical case - a complete packet.
         */
        if (tmd.tmd1.stp && tmd.tmd1.enp)
        {
            const unsigned cb = 4096 - tmd.tmd1.bcnt;
            Log(("#%d pcnetAsyncTransmit: stp&enp: cb=%d xmtrc=%#x\n", PCNET_INST_NR, cb, CSR_XMTRC(pThis)));
            STAM_COUNTER_INC(&pThis->StatTransmitCase1);

            if (RT_LIKELY(pcnetIsLinkUp(pThis) || fLoopback))
            {
                /* From the manual: ``A zero length buffer is acceptable as
                 * long as it is not the last buffer in a chain (STP = 0 and
                 * ENP = 1).'' That means that the first buffer might have a
                 * zero length if it is not the last one in the chain. */
                if (RT_LIKELY(cb <= MAX_FRAME))
                {
                    rc = pcnetXmitAllocBuf(pThis, pThisCC, cb, fLoopback, &SgLoop, &pSgBuf);
                    if (RT_SUCCESS(rc))
                    {
                        pcnetXmitRead1st(pDevIns, pThis, PHYSADDR(pThis, tmd.tmd0.tbadr), cb, pSgBuf);
                        rc = pcnetXmitSendBuf(pDevIns, pThis, pThisCC, fLoopback, pSgBuf, fOnWorkerThread);
                    }
                    else if (rc == VERR_TRY_AGAIN)
                    {
                        STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
                        return VINF_SUCCESS;
                    }
                    if (RT_FAILURE(rc))
                        pcnetXmitFailTMDLinkDown(pThis, &tmd);
                }
                else if (cb == 4096)
                {
                    /* The Windows NT4 pcnet driver sometimes marks the first
                     * unused descriptor as owned by us. Ignore that (by
                     * passing it back). Do not update the ring counter in this
                     * case (otherwise that driver becomes even more confused,
                     * which causes transmit to stall for about 10 seconds).
                     * This is just a workaround, not a final solution. */
                    /* r=frank: IMHO this is the correct implementation. The
                     * manual says: ``If the OWN bit is set and the buffer
                     * length is 0, the OWN bit will be cleared. In the C-LANCE
                     * the buffer length of 0 is interpreted as a 4096-byte
                     * buffer.'' */
                    /* r=michaln: Perhaps not quite right. The C-LANCE (Am79C90)
                     * datasheet explains that the old LANCE (Am7990) ignored
                     * the top four bits next to BCNT and a count of 0 was
                     * interpreted as 4096. In the C-LANCE, that is still the
                     * case if the top bits are all ones. If all 16 bits are
                     * zero, the C-LANCE interprets it as zero-length transmit
                     * buffer. It's not entirely clear if the later models
                     * (PCnet-ISA, PCnet-PCI) behave like the C-LANCE or not.
                     * It is possible that the actual behavior of the C-LANCE
                     * and later hardware is that the buffer lengths are *16-bit*
                     * two's complement numbers between 0 and 4096. AMD's drivers
                     * in fact generally treat the length as a 16-bit quantity. */
                    LogRel(("PCnet#%d: pcnetAsyncTransmit: illegal 4kb frame -> ignoring\n", PCNET_INST_NR));
                    pcnetTmdStorePassHost(pDevIns, pThis, &tmd, PHYSADDR(pThis, CSR_CXDA(pThis)));
                    break;
                }
                else
                {
                    /* Signal error, as this violates the Ethernet specs. */
                    /** @todo check if the correct error is generated. */
                    LogRel(("PCnet#%d: pcnetAsyncTransmit: illegal 4kb frame -> signalling error\n", PCNET_INST_NR));

                    pcnetXmitFailTMDGeneric(pThis, &tmd);
                }
            }
            else
                pcnetXmitFailTMDLinkDown(pThis, &tmd);

            /* Write back the TMD and pass it to the host (clear own bit). */
            pcnetTmdStorePassHost(pDevIns, pThis, &tmd, PHYSADDR(pThis, CSR_CXDA(pThis)));

            /* advance the ring counter register */
            if (CSR_XMTRC(pThis) < 2)
                CSR_XMTRC(pThis) = CSR_XMTRL(pThis);
            else
                CSR_XMTRC(pThis)--;
        }
        else if (tmd.tmd1.stp)
        {
            STAM_COUNTER_INC(&pThis->StatTransmitCase2);

            /*
             * Read TMDs until end-of-packet or tdte poll fails (underflow).
             *
             * We allocate a maximum sized buffer here since we do not wish to
             * waste time finding out how much space we actually need even if
             * we could reliably do that on SMP guests.
             */
            unsigned cb = 4096 - tmd.tmd1.bcnt;
            rc = pcnetXmitAllocBuf(pThis, pThisCC, RT_MAX(MAX_FRAME, cb), fLoopback, &SgLoop, &pSgBuf);
            if (rc == VERR_TRY_AGAIN)
            {
                STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
                return VINF_SUCCESS;
            }

            bool fDropFrame = RT_FAILURE(rc);
            if (!fDropFrame)
                pcnetXmitRead1st(pDevIns, pThis, PHYSADDR(pThis, tmd.tmd0.tbadr), cb, pSgBuf);

            for (;;)
            {
                /*
                 * Advance the ring counter register and check the next tmd.
                 */
#ifdef LOG_ENABLED
                const uint32_t iStart        = CSR_XMTRC(pThis);
#endif
                const uint32_t GCPhysPrevTmd = PHYSADDR(pThis, CSR_CXDA(pThis));
                if (CSR_XMTRC(pThis) < 2)
                    CSR_XMTRC(pThis) = CSR_XMTRL(pThis);
                else
                    CSR_XMTRC(pThis)--;

                TMD tmdNext;
                if (!pcnetTdtePoll(pDevIns, pThis, &tmdNext))
                {
                    /*
                     * Underflow!
                     */
                    tmd.tmd2.buff = tmd.tmd2.uflo = tmd.tmd1.err = 1;
                    pThis->aCSR[0] |= 0x0200;        /* set TINT */
                    /* Don't allow the guest to clear TINT before reading it */
                    pThis->u16CSR0LastSeenByGuest &= ~0x0200;
                    if (!CSR_DXSUFLO(pThis))         /* stop on xmit underflow */
                        pThis->aCSR[0] &= ~0x0010;   /* clear TXON */
                    pcnetTmdStorePassHost(pDevIns, pThis, &tmd, GCPhysPrevTmd);
                    AssertMsgFailed(("pcnetAsyncTransmit: Underflow!!!\n"));
                    pcnetXmitFreeBuf(pThisCC, fLoopback, pSgBuf);
                    break;
                }

                /* release & save the previous tmd, pass it to the host */
                pcnetTmdStorePassHost(pDevIns, pThis, &tmd, GCPhysPrevTmd);

                /*
                 * The next tmd is already loaded.
                 */
#ifdef VBOX_WITH_STATISTICS
                cBuffers++;
#endif
                tmd = tmdNext;
                cb = 4096 - tmd.tmd1.bcnt;
                if (   !fDropFrame
                    && pSgBuf->cbUsed + cb <= pSgBuf->cbAvailable)
                    pcnetXmitReadMore(pDevIns, PHYSADDR(pThis, tmd.tmd0.tbadr), cb, pSgBuf);
                else
                {
                    AssertMsg(fDropFrame, ("pcnetAsyncTransmit: Frame is too big!!! %d bytes\n", pSgBuf->cbUsed + cb));
                    fDropFrame = true;
                }

                /*
                 * Done already?
                 */
                if (tmd.tmd1.enp)
                {
                    Log(("#%d pcnetAsyncTransmit: stp: cb=%d xmtrc=%#x-%#x\n", PCNET_INST_NR,
                         pSgBuf ? pSgBuf->cbUsed : 0, iStart, CSR_XMTRC(pThis)));
                    if (!fDropFrame && (pcnetIsLinkUp(pThis) || fLoopback))
                    {
                        rc = pcnetXmitSendBuf(pDevIns, pThis, pThisCC, fLoopback, pSgBuf, fOnWorkerThread);
                        fDropFrame = RT_FAILURE(rc);
                    }
                    else
                        pcnetXmitFreeBuf(pThisCC, fLoopback, pSgBuf);
                    if (fDropFrame)
                        pcnetXmitFailTMDLinkDown(pThis, &tmd);

                    /* Write back the TMD, pass it to the host */
                    pcnetTmdStorePassHost(pDevIns, pThis, &tmd, PHYSADDR(pThis, CSR_CXDA(pThis)));

                    /* advance the ring counter register */
                    if (CSR_XMTRC(pThis) < 2)
                        CSR_XMTRC(pThis) = CSR_XMTRL(pThis);
                    else
                        CSR_XMTRC(pThis)--;
                    break;
                }
            } /* the loop */
        }
        else
        {
            /*
             * We underflowed in a previous transfer, or the driver is giving us shit.
             * Simply stop the transmitting for now.
             */
            /** @todo according to the specs we're supposed to clear the own bit and move on to the next one. */
            Log(("#%d pcnetAsyncTransmit: guest is giving us shit!\n", PCNET_INST_NR));
            break;
        }
        /* Update TDMD, TXSTRT and TINT. */
        pThis->aCSR[0] &= ~0x0008;       /* clear TDMD */

        pThis->aCSR[4] |=  0x0008;       /* set TXSTRT */
        if (    !CSR_TOKINTD(pThis)      /* Transmit OK Interrupt Disable, no infl. on errors. */
            ||  (CSR_LTINTEN(pThis) && tmd.tmd1.ltint)
            ||  tmd.tmd1.err)
        {
            cFlushIrq++;
        }

        /** @todo should we continue after an error (tmd.tmd1.err) or not? */

        STAM_COUNTER_INC(&pThis->aStatXmitChainCounts[RT_MIN(cBuffers,
                                                      RT_ELEMENTS(pThis->aStatXmitChainCounts)) - 1]);
        if (--cMax == 0)
            break;
    } while (CSR_TXON(pThis));          /* transfer on */

    if (cFlushIrq)
    {
        STAM_COUNTER_INC(&pThis->aStatXmitFlush[RT_MIN(cFlushIrq, RT_ELEMENTS(pThis->aStatXmitFlush)) - 1]);
        /* The WinXP PCnet driver has apparently a bug: It sets CSR0.TDMD _before_
         * it clears CSR0.TINT. This can lead to a race where the driver clears
         * CSR0.TINT right after it was set by the device. The driver waits until
         * CSR0.TINT is set again but this will never happen. So prevent clearing
         * this bit as long as the driver didn't read it. See @bugref{5288}. */
        pThis->aCSR[0] |= 0x0200;    /* set TINT */
        /* Don't allow the guest to clear TINT before reading it */
        pThis->u16CSR0LastSeenByGuest &= ~0x0200;
        pcnetUpdateIrq(pDevIns, pThis);
    }

    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);

    return VINF_SUCCESS;
}


/**
 * Transmit pending descriptors.
 *
 * @returns VBox status code.  VERR_TRY_AGAIN is returned if we're busy.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The PCnet shared instance data.
 * @param   pThisCC             The PCNet state data for the current context.
 * @param   fOnWorkerThread     Whether we're on a worker thread or on an EMT.
 */
static int pcnetXmitPending(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, bool fOnWorkerThread)
{
    RT_NOREF_PV(fOnWorkerThread);
    int rc;

    /*
     * Grab the xmit lock of the driver as well as the PCnet device state.
     */
    PPDMINETWORKUP pDrv = pThisCC->pDrv;
    if (pDrv)
    {
        rc = pDrv->pfnBeginXmit(pDrv, false /*fOnWorkerThread*/);
        if (RT_FAILURE(rc))
            return rc;
    }
    rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    if (RT_SUCCESS(rc))
    {
        /** @todo check if we're supposed to suspend now. */
        /*
         * Do the transmitting.
         */
        int rc2 = pcnetAsyncTransmit(pDevIns, pThis, pThisCC, false /*fOnWorkerThread*/);
        AssertReleaseRC(rc2);

        /*
         * Release the locks.
         */
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    }
    else
        AssertLogRelRC(rc);
    if (pDrv)
        pDrv->pfnEndXmit(pDrv);

    return rc;
}


/**
 * Poll for changes in RX and TX descriptor rings.
 */
static void pcnetPollRxTx(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC)
{
    if (CSR_RXON(pThis))
    {
        /*
         * The second case is important for pcnetWaitReceiveAvail(): If CSR_CRST(pThis) was
         * true but pcnetR3CanReceive() returned false for some other reason we need to check
         * _now_ if we have to wakeup pcnetWaitReceiveAvail().
         */
        if (   HOST_IS_OWNER(CSR_CRST(pThis))  /* only poll RDTEs if none available or ... */
            || pThis->fMaybeOutOfSpace)        /* ... for waking up pcnetWaitReceiveAvail() */
            pcnetRdtePoll(pDevIns, pThis);
    }

    if (CSR_TDMD(pThis) || (CSR_TXON(pThis) && !CSR_DPOLL(pThis)))
        pcnetTransmit(pDevIns, pThis, pThisCC);
}


/**
 * Start the poller timer.
 * Poll timer interval is fixed to 500Hz. Don't stop it.
 * @thread EMT, TAP.
 */
static void pcnetPollTimerStart(PPDMDEVINS pDevIns, PPCNETSTATE pThis)
{
    PDMDevHlpTimerSetMillies(pDevIns, pThis->hTimerPoll, 2);
}


/**
 * Update the poller timer.
 * @thread EMT.
 */
static void pcnetPollTimer(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC)
{
    STAM_PROFILE_ADV_START(&pThis->StatPollTimer, a);

#ifdef LOG_ENABLED
    TMD dummy;
    if (CSR_STOP(pThis) || CSR_SPND(pThis))
        Log2(("#%d pcnetPollTimer time=%#010llx CSR_STOP=%d CSR_SPND=%d\n",
             PCNET_INST_NR, RTTimeMilliTS(), CSR_STOP(pThis), CSR_SPND(pThis)));
    else
        Log2(("#%d pcnetPollTimer time=%#010llx TDMD=%d TXON=%d POLL=%d TDTE=%d TDRA=%#x\n",
             PCNET_INST_NR, RTTimeMilliTS(), CSR_TDMD(pThis), CSR_TXON(pThis),
             !CSR_DPOLL(pThis), pcnetTdtePoll(pDevIns, pThis, &dummy), pThis->GCTDRA));
    Log2(("#%d pcnetPollTimer: CSR_CXDA=%#x CSR_XMTRL=%d CSR_XMTRC=%d\n",
          PCNET_INST_NR, CSR_CXDA(pThis), CSR_XMTRL(pThis), CSR_XMTRC(pThis)));
#endif
#ifdef LOG_ENABLED
    if (CSR_CXDA(pThis))
    {
        TMD tmd;
        pcnetTmdLoadAll(pDevIns, pThis, &tmd, PHYSADDR(pThis, CSR_CXDA(pThis)));
        Log10(("#%d pcnetPollTimer: TMDLOAD %#010x\n", PCNET_INST_NR, PHYSADDR(pThis, CSR_CXDA(pThis))));
        PRINT_TMD(&tmd);
    }
#endif
    if (CSR_TDMD(pThis))
        pcnetTransmit(pDevIns, pThis, pThisCC);

    pcnetUpdateIrq(pDevIns, pThis);

    /* If the receive thread is waiting for new descriptors, poll TX/RX even if polling
     * disabled. We wouldn't need to poll for new TX descriptors in that case but it will
     * not hurt as waiting for RX descriptors should happen very seldom */
    if (RT_LIKELY(   !CSR_STOP(pThis)
                  && !CSR_SPND(pThis)
                  && (   !CSR_DPOLL(pThis)
                      || pThis->fMaybeOutOfSpace)))
    {
        /* We ensure that we poll at least every 2ms (500Hz) but not more often than
         * 5000 times per second. This way we completely prevent the overhead from
         * heavy reprogramming the timer which turned out to be very CPU-intensive.
         * The drawback is that csr46 and csr47 are not updated properly anymore
         * but so far I have not seen any guest depending on these values. The 2ms
         * interval is the default polling interval of the PCnet card (65536/33MHz). */
        uint64_t u64Now = PDMDevHlpTimerGet(pDevIns, pThis->hTimerPoll);
        if (RT_UNLIKELY(u64Now - pThis->u64LastPoll > 200000))
        {
            pThis->u64LastPoll = u64Now;
            pcnetPollRxTx(pDevIns, pThis, pThisCC);
        }
        if (!PDMDevHlpTimerIsActive(pDevIns, pThis->hTimerPoll))
            pcnetPollTimerStart(pDevIns, pThis);
    }
    STAM_PROFILE_ADV_STOP(&pThis->StatPollTimer, a);
}


static VBOXSTRICTRC pcnetCSRWriteU16(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, uint32_t u32RAP, uint32_t val)
{
    VBOXSTRICTRC rc  = VINF_SUCCESS;
    Log8(("#%d pcnetCSRWriteU16: rap=%d val=%#06x\n", PCNET_INST_NR, u32RAP, val));
    switch (u32RAP)
    {
        case 0:
            {
                uint16_t csr0 = pThis->aCSR[0];
                /* Clear any interrupt flags.
                 * Don't clear an interrupt flag which was not seen by the guest yet. */
                csr0 &= ~(val  &  0x7f00  & pThis->u16CSR0LastSeenByGuest);
                csr0  =  (csr0 & ~0x0040) | (val  & 0x0048);
                val   =  (val  &  0x007f) | (csr0 & 0x7f00);

                /* Iff STOP, STRT and INIT are set, clear STRT and INIT */
                if ((val & 7) == 7)
                    val &= ~3;

                Log(("#%d CSR0: old=%#06x new=%#06x\n", PCNET_INST_NR, pThis->aCSR[0], csr0));

#ifndef IN_RING3
                if (!(csr0 & 0x0001/*init*/) && (val & 1))
                {
                    Log(("#%d pcnetCSRWriteU16: pcnetR3Init requested => HC\n", PCNET_INST_NR));
                    return VINF_IOM_R3_IOPORT_WRITE;
                }
#endif
                pThis->aCSR[0] = csr0;

                if (!CSR_STOP(pThis) && (val & 4))
                    pcnetStop(pDevIns, pThis, pThisCC);

#ifdef IN_RING3
                if (!CSR_INIT(pThis) && (val & 1))
                {
                    bool    fDelayInit = false;

                    /* Many PCnet drivers disable PCI bus mastering before setting the INIT bit and
                     * then immediately enable it back again. This is done to work around a silicon
                     * bug that could cause a PCI bus hang under some circumstances. The bug only
                     * existed in the early PCI chips (Am79C970 PCnet-PCI) but many drivers apply the
                     * workaround to all PCnet PCI models. Drivers written by AMD generally do this
                     * (DOS, Windows, OS/2). PCnet drivers in Windows 2000 and XP are new enough to
                     * not apply the workaround to our emulated PCnet-PCI II (Am79C970A) and
                     * PCnet-FAST III (Am79C973).
                     *
                     * The AMDPCnet32 drivers for NeXTSTEP/OpenStep (notably OS 4.2) completely fail
                     * unless we delay the initialization until after bus mastering is re-enabled.
                     */
                    if (PCNET_IS_PCI(pThis))
                    {
                        PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
                        uint8_t uCmd = PDMPciDevGetByte(pPciDev, 0x04);

                        /* Recognize situation with PCI bus mastering disabled and setting
                         * INIT bit without also setting STRT.
                         */
                        if (!(uCmd & 4) && !(val & 2))
                            fDelayInit = true;
                    }

                    if (!fDelayInit)
                        pcnetR3Init(pDevIns, pThis, pThisCC);
                    else
                    {
                        LogRel(("PCnet#%d: Delaying INIT due to disabled PCI bus mastering\n", PCNET_INST_NR));
                        pThis->aCSR[0] |=  0x0001;  /* Set INIT and MERR bits. */
                        pThis->aCSR[6] = 1;         /* Set a flag in read-only CSR6. */
                    }
                }
#endif

                if (!CSR_STRT(pThis) && (val & 2))
                    pcnetStart(pDevIns, pThis);

                if (CSR_TDMD(pThis))
                    pcnetTransmit(pDevIns, pThis, pThisCC);

                return rc;
            }
        case 2:  /* IADRH */
            if (PCNET_IS_ISA(pThis))
                val &= 0x00ff;  /* Upper 8 bits ignored on ISA chips. */
            RT_FALL_THRU();
        case 1:  /* IADRL */
        case 8:  /* LADRF  0..15 */
        case 9:  /* LADRF 16..31 */
        case 10: /* LADRF 32..47 */
        case 11: /* LADRF 48..63 */
        case 12: /* PADR   0..15 */
        case 13: /* PADR  16..31 */
        case 14: /* PADR  32..47 */
        case 18: /* CRBAL */
        case 19: /* CRBAU */
        case 20: /* CXBAL */
        case 21: /* CXBAU */
        case 22: /* NRBAL */
        case 23: /* NRBAU */
        case 26: /* NRDAL */
        case 27: /* NRDAU */
        case 28: /* CRDAL */
        case 29: /* CRDAU */
        case 32: /* NXDAL */
        case 33: /* NXDAU */
        case 34: /* CXDAL */
        case 35: /* CXDAU */
        case 36: /* NNRDL */
        case 37: /* NNRDU */
        case 38: /* NNXDL */
        case 39: /* NNXDU */
        case 40: /* CRBCL */
        case 41: /* CRBCU */
        case 42: /* CXBCL */
        case 43: /* CXBCU */
        case 44: /* NRBCL */
        case 45: /* NRBCU */
        case 46: /* POLL */
        case 47: /* POLLINT */
        case 72: /* RCVRC */
        case 74: /* XMTRC */
        case 112: /* MISSC */
            if (CSR_STOP(pThis) || CSR_SPND(pThis))
                break;
            else
            {
                Log(("#%d: WRITE CSR%d, %#06x, ignoring!!\n", PCNET_INST_NR, u32RAP, val));
                return rc;
            }
        case 3: /* Interrupt Mask and Deferral Control */
            break;
        case 4: /* Test and Features Control */
            pThis->aCSR[4] &= ~(val & 0x026a);
            val &= ~0x026a;
            val |= pThis->aCSR[4] & 0x026a;
            break;
        case 5: /* Extended Control and Interrupt 1 */
            pThis->aCSR[5] &= ~(val & 0x0a90);
            val &= ~0x0a90;
            val |= pThis->aCSR[5] & 0x0a90;
            break;
        case 7: /* Extended Control and Interrupt 2 */
            {
                uint16_t csr7 = pThis->aCSR[7];
                csr7 &=        ~0x0400 ;
                csr7 &= ~(val & 0x0800);
                csr7 |=  (val & 0x0400);
                pThis->aCSR[7] = csr7;
                return rc;
            }
        case 15: /* Mode */
            if ((pThis->aCSR[15] & 0x8000) != (uint16_t)(val & 0x8000) && pThis->fDriverAttached)
            {
                Log(("#%d: promiscuous mode changed to %d\n", PCNET_INST_NR, !!(val & 0x8000)));
#ifndef IN_RING3
                return VINF_IOM_R3_IOPORT_WRITE;
#else
                /* check for promiscuous mode change */
                if (pThisCC->pDrv)
                    pThisCC->pDrv->pfnSetPromiscuousMode(pThisCC->pDrv, !!(val & 0x8000));
#endif
            }
            break;
        case 16: /* IADRL */
            return pcnetCSRWriteU16(pDevIns, pThis, pThisCC, 1, val);
        case 17: /* IADRH */
            return pcnetCSRWriteU16(pDevIns, pThis, pThisCC, 2, val);

        /*
         * 24 and 25 are the Base Address of Receive Descriptor.
         * We combine and mirror these in GCRDRA.
         */
        case 24: /* BADRL */
        case 25: /* BADRU */
            if (!CSR_STOP(pThis) && !CSR_SPND(pThis))
            {
                Log(("#%d: WRITE CSR%d, %#06x, ignoring!!\n", PCNET_INST_NR, u32RAP, val));
                return rc;
            }
            if (u32RAP == 24)
                pThis->GCRDRA = (pThis->GCRDRA & 0xffff0000) | (val & 0x0000ffff);
            else
                pThis->GCRDRA = (pThis->GCRDRA & 0x0000ffff) | ((val & 0x0000ffff) << 16);
            Log(("#%d: WRITE CSR%d, %#06x => GCRDRA=%08x (alt init)\n", PCNET_INST_NR, u32RAP, val, pThis->GCRDRA));
            if (pThis->GCRDRA & (pThis->iLog2DescSize - 1))
                LogRel(("PCnet#%d: Warning: Misaligned RDRA (GCRDRA=%#010x)\n", PCNET_INST_NR, pThis->GCRDRA));
            break;

        /*
         * 30 & 31 are the Base Address of Transmit Descriptor.
         * We combine and mirrorthese in GCTDRA.
         */
        case 30: /* BADXL */
        case 31: /* BADXU */
            if (!CSR_STOP(pThis) && !CSR_SPND(pThis))
            {
                Log(("#%d: WRITE CSR%d, %#06x !!\n", PCNET_INST_NR, u32RAP, val));
                return rc;
            }
            if (u32RAP == 30)
                pThis->GCTDRA = (pThis->GCTDRA & 0xffff0000) | (val & 0x0000ffff);
            else
                pThis->GCTDRA = (pThis->GCTDRA & 0x0000ffff) | ((val & 0x0000ffff) << 16);
            Log(("#%d: WRITE CSR%d, %#06x => GCTDRA=%08x (alt init)\n", PCNET_INST_NR, u32RAP, val, pThis->GCTDRA));
            if (pThis->GCTDRA & (pThis->iLog2DescSize - 1))
                LogRel(("PCnet#%d: Warning: Misaligned TDRA (GCTDRA=%#010x)\n", PCNET_INST_NR, pThis->GCTDRA));
            break;

        case 58: /* Software Style */
            rc = pcnetBCRWriteU16(pDevIns, pThis, BCR_SWS, val);
            break;

        /*
         * Registers 76 and 78 aren't stored correctly (see todos), but I'm don't dare
         * try fix that right now. So, as a quick hack for 'alt init' I'll just correct them here.
         */
        case 76: /* RCVRL */ /** @todo call pcnetR3UpdateRingHandlers */
                             /** @todo receive ring length is stored in two's complement! */
        case 78: /* XMTRL */ /** @todo call pcnetR3UpdateRingHandlers */
                             /** @todo transmit ring length is stored in two's complement! */
            if (!CSR_STOP(pThis) && !CSR_SPND(pThis))
            {
                Log(("#%d: WRITE CSR%d, %#06x !!\n", PCNET_INST_NR, u32RAP, val));
                return rc;
            }
            Log(("#%d: WRITE CSR%d, %#06x (hacked %#06x) (alt init)\n", PCNET_INST_NR,
                 u32RAP, val, 1 + ~(uint16_t)val));
            val = 1 + ~(uint16_t)val;

            /*
             * HACK ALERT! Set the counter registers too.
             */
            pThis->aCSR[u32RAP - 4] = val;
            break;

        default:
            return rc;
    }
    pThis->aCSR[u32RAP] = val;
    return rc;
}

/**
 * Encode a 32-bit link speed into a custom 16-bit floating-point value
 */
static uint32_t pcnetLinkSpd(uint32_t speed)
{
    unsigned    exp = 0;

    while (speed & 0xFFFFE000)
    {
        speed /= 10;
        ++exp;
    }
    return (exp << 13) | speed;
}

static VBOXSTRICTRC pcnetCSRReadU16(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, uint32_t u32RAP, uint32_t *pu32)
{
    uint32_t val;
    switch (u32RAP)
    {
        case 0:
            /* Check if delayed initialization needs to run. */
            if (RT_UNLIKELY(pThis->aCSR[6] == 1))
            {
#ifndef IN_RING3
                return VINF_IOM_R3_IOPORT_READ;
#else
                /* This is the second half of delayed initialization required
                 * to work around guest drivers that temporarily disable PCI bus
                 * mastering around setting the INIT bit in CSR0.
                 * See pcnetCSRWriteU16() for details.
                 */
                pcnetR3Init(pDevIns, pThis, pThisCC);
                Assert(pThis->aCSR[6] != 1);
#endif
            }
            pcnetUpdateIrq(pDevIns, pThis);
            val = pThis->aCSR[0];
            val |= (val & 0x7800) ? 0x8000 : 0;
            pThis->u16CSR0LastSeenByGuest = val;
            break;
        case 16:
            return pcnetCSRReadU16(pDevIns, pThis, pThisCC, 1, pu32);
        case 17:
            return pcnetCSRReadU16(pDevIns, pThis, pThisCC, 2, pu32);
        case 58:
            *pu32 = pcnetBCRReadU16(pThis, BCR_SWS);
            return VINF_SUCCESS;
        case 68:    /* Custom register to pass link speed to driver */
            *pu32 = pcnetLinkSpd(pThis->u32LinkSpeed);
            return VINF_SUCCESS;
        case 88:
            val = pThis->aCSR[89];
            val <<= 16;
            val |= pThis->aCSR[88];
            break;
        default:
            val = pThis->aCSR[u32RAP];
    }
    *pu32 = val;
    Log8(("#%d pcnetCSRReadU16: rap=%d val=%#06x\n", PCNET_INST_NR, u32RAP, val));
    return VINF_SUCCESS;
}

static VBOXSTRICTRC pcnetBCRWriteU16(PPDMDEVINS pDevIns, PPCNETSTATE pThis, uint32_t u32RAP, uint32_t val)
{
    u32RAP &= 0x7f;
    Log7(("#%d pcnetBCRWriteU16: rap=%d val=%#06x\n", PCNET_INST_NR, u32RAP, val));
    switch (u32RAP)
    {
        case BCR_SWS:
            if (!(CSR_STOP(pThis) || CSR_SPND(pThis)))
                return VINF_SUCCESS;
            val &= ~0x0300;
            switch (val & 0x00ff)
            {
                default:
                    Log(("#%d Bad SWSTYLE=%#04x\n", PCNET_INST_NR, val & 0xff));
                    RT_FALL_THRU();
                case 0:
                    val |= 0x0200; /* 16 bit */
                    pThis->iLog2DescSize = 3;
                    pThis->GCUpperPhys   = (0xff00 & (uint32_t)pThis->aCSR[2]) << 16;
                    break;
                case 1:
                    val |= 0x0100; /* 32 bit */
                    pThis->iLog2DescSize = 4;
                    pThis->GCUpperPhys   = 0;
                    break;
                case 2:
                case 3:
                    val |= 0x0300; /* 32 bit */
                    pThis->iLog2DescSize = 4;
                    pThis->GCUpperPhys   = 0;
                    break;
            }
            Log(("#%d BCR_SWS=%#06x\n", PCNET_INST_NR, val));
            pThis->aCSR[58] = val;
            RT_FALL_THRU();
        case BCR_LNKST:
        case BCR_LED1:
        case BCR_LED2:
        case BCR_LED3:
        case BCR_MC:
        case BCR_FDC:
        case BCR_BSBC:
        case BCR_EECAS:
        case BCR_PLAT:
        case BCR_MIICAS:
        case BCR_MIIADDR:
            pThis->aBCR[u32RAP] = val;
            break;

        case BCR_STVAL:
            val &= 0xffff;
            pThis->aBCR[BCR_STVAL] = val;
            if (pThis->uDevType == DEV_AM79C973)
                PDMDevHlpTimerSetNano(pDevIns, pThis->hTimerSoftInt, 12800U * val);
            break;

        case BCR_MIIMDR:
            pThis->aMII[pThis->aBCR[BCR_MIIADDR] & 0x1f] = val;
            Log12(("#%d pcnet: mii write %d <- %#x\n", PCNET_INST_NR, pThis->aBCR[BCR_MIIADDR] & 0x1f, val));
            break;

        default:
            break;
    }
    return VINF_SUCCESS;
}

static uint32_t pcnetMIIReadU16(PPCNETSTATE pThis, uint32_t miiaddr)
{
    uint32_t val;
    bool autoneg, duplex, fast, isolate;
    STAM_COUNTER_INC(&pThis->StatMIIReads);

    /* If the DANAS (BCR32.7) bit is set, the MAC does not do any
     * auto-negotiation and the PHY must be set up explicitly. DANAS
     * effectively disables most other BCR32 bits.
     */
    if (pThis->aBCR[BCR_MIICAS] & 0x80)
    {
        /* PHY controls auto-negotiation. */
        autoneg = duplex = fast = 1;
    }
    else
    {
        /* BCR32 controls auto-negotiation. */
        autoneg = (pThis->aBCR[BCR_MIICAS] & 0x20) != 0;
        duplex  = (pThis->aBCR[BCR_MIICAS] & 0x10) != 0;
        fast    = (pThis->aBCR[BCR_MIICAS] & 0x08) != 0;
    }

    /* Electrically isolating the PHY mostly disables it. */
    isolate = (pThis->aMII[0] & RT_BIT(10)) != 0;

    switch (miiaddr)
    {
        case 0:
            /* MII basic mode control register. */
            val = 0;
            if (autoneg)
                val |= 0x1000;  /* Enable auto negotiation. */
            if (fast)
                val |= 0x2000;  /* 100 Mbps */
            if (duplex) /* Full duplex forced */
                val |= 0x0100;  /* Full duplex */
            if (isolate)    /* PHY electrically isolated. */
                val |= 0x0400;  /* Isolated */
            break;

        case 1:
            /* MII basic mode status register. */
            val = 0x7800    /* Can do 100mbps FD/HD and 10mbps FD/HD. */
                | 0x0040    /* Mgmt frame preamble not required. */
                | 0x0020    /* Auto-negotiation complete. */
                | 0x0008    /* Able to do auto-negotiation. */
                | 0x0004    /* Link up. */
                | 0x0001;   /* Extended Capability, i.e. registers 4+ valid. */
            if (!pcnetIsLinkUp(pThis) || isolate) {
                val &= ~(0x0020 | 0x0004);
                pThis->cLinkDownReported++;
            }
            if (!autoneg) {
                /* Auto-negotiation disabled. */
                val &= ~(0x0020 | 0x0008);
                if (duplex)
                    /* Full duplex forced. */
                    val &= ~0x2800;
                else
                    /* Half duplex forced. */
                    val &= ~0x5000;

                if (fast)
                    /* 100 Mbps forced */
                    val &= ~0x1800;
                else
                    /* 10 Mbps forced */
                    val &= ~0x6000;
            }
            break;

        case 2:
            /* PHY identifier 1. */
            val = 0x22;     /* Am79C874/AC101 PHY */
            break;

        case 3:
            /* PHY identifier 2. */
            val = 0x561b;   /* Am79C874/AC101 PHY */
            break;

        case 4:
            /* Advertisement control register. */
            val =   0x01e0  /* Try 100mbps FD/HD and 10mbps FD/HD. */
#if 0
                // Advertising flow control is a) not the default, and b) confuses
                // the link speed detection routine in Windows PCnet driver
                  | 0x0400  /* Try flow control. */
#endif
                  | 0x0001; /* CSMA selector. */
            break;

        case 5:
            /* Link partner ability register. */
            if (pcnetIsLinkUp(pThis) && !isolate)
                val =   0x8000  /* Next page bit. */
                      | 0x4000  /* Link partner acked us. */
                      | 0x0400  /* Can do flow control. */
                      | 0x01e0  /* Can do 100mbps FD/HD and 10mbps FD/HD. */
                      | 0x0001; /* Use CSMA selector. */
            else
            {
                val = 0;
                pThis->cLinkDownReported++;
            }
            break;

        case 6:
            /* Auto negotiation expansion register. */
            if (pcnetIsLinkUp(pThis) && !isolate)
                val =   0x0008  /* Link partner supports npage. */
                      | 0x0004  /* Enable npage words. */
                      | 0x0001; /* Can do N-way auto-negotiation. */
            else
            {
                val = 0;
                pThis->cLinkDownReported++;
            }
            break;

        case 18:
            /* Diagnostic Register (FreeBSD pcn/ac101 driver reads this). */
            if (pcnetIsLinkUp(pThis) && !isolate)
            {
                val =   0x0100  /* Receive PLL locked. */
                      | 0x0200; /* Signal detected. */

                if (autoneg)
                {
                    val |=   0x0400     /* 100Mbps rate. */
                           | 0x0800;    /* Full duplex. */
                }
                else
                {
                    if (fast)
                        val |= 0x0400;  /* 100Mbps rate. */
                    if (duplex)
                        val |= 0x0800;  /* Full duplex. */
                }
            }
            else
            {
                val = 0;
                pThis->cLinkDownReported++;
            }
            break;

        default:
            val = 0;
            break;
    }

    Log12(("#%d pcnet: mii read %d -> %#x\n", PCNET_INST_NR, miiaddr, val));
    return val;
}

static uint32_t pcnetBCRReadU16(PPCNETSTATE pThis, uint32_t u32RAP)
{
    uint32_t val;
    u32RAP &= 0x7f;
    switch (u32RAP)
    {
        case BCR_LNKST:
        case BCR_LED1:
        case BCR_LED2:
        case BCR_LED3:
            val = pThis->aBCR[u32RAP] & ~0x8000;
            /* Clear LNKSTE if we're not connected or if we've just loaded a VM state. */
            if (!pThis->fDriverAttached || pThis->fLinkTempDown || !pThis->fLinkUp)
            {
                if (u32RAP == 4)
                    pThis->cLinkDownReported++;
                val &= ~0x40;
            }
            /* AMD NDIS 5.0 driver programs BCR4 to indicate link state and polls
             * the LED bit (bit 15) to determine current link status.
             */
            val |= (val & 0x017f & pThis->u32Lnkst) ? 0x8000 : 0;
            break;

        case BCR_MIIMDR:
            if ((pThis->uDevType == DEV_AM79C973) && (pThis->aBCR[BCR_MIIADDR] >> 5 & 0x1f) == 0)
            {
                uint32_t miiaddr = pThis->aBCR[BCR_MIIADDR] & 0x1f;
                val = pcnetMIIReadU16(pThis, miiaddr);
            }
            else
                val = 0xffff;
            break;

        default:
            val = u32RAP < BCR_MAX_RAP ? pThis->aBCR[u32RAP] : 0;
            break;
    }
    Log7(("#%d pcnetBCRReadU16: rap=%d val=%#06x\n", PCNET_INST_NR, u32RAP, val));
    return val;
}

#ifdef IN_RING3 /* move down */
static void pcnetR3HardReset(PPDMDEVINS pDevIns, PPCNETSTATE pThis)
{
    int      i;
    uint16_t checksum;

    /* Lower any raised interrupts, see @bugref(9556) */
    if (RT_UNLIKELY(pThis->iISR))
    {
        pThis->iISR = 0;
        if (!PCNET_IS_ISA(pThis))
        {
            Log(("#%d INTA=%d\n", PCNET_INST_NR, pThis->iISR));
            PDMDevHlpPCISetIrq(pDevIns, 0, pThis->iISR);
        }
        else
        {
            Log(("#%d IRQ=%d, state=%d\n", PCNET_INST_NR, pThis->uIsaIrq, pThis->iISR));
            PDMDevHlpISASetIrq(pDevIns, pThis->uIsaIrq, pThis->iISR);
        }
    }
    /* Initialize the PROM */
    Assert(sizeof(pThis->MacConfigured) == 6);
    memcpy(pThis->aPROM, &pThis->MacConfigured, sizeof(pThis->MacConfigured));
    pThis->aPROM[ 8] = 0x00;
    pThis->aPROM[12] = pThis->aPROM[13] = 0x00;
    if (pThis->uDevType == DEV_AM79C960_EB)
    {
        pThis->aPROM[14] = 0x52;
        pThis->aPROM[15] = 0x44; /* NI6510 EtherBlaster 'RD' signature. */
    }
    else
        pThis->aPROM[14] = pThis->aPROM[15] = 0x57; /* NE2100 'WW' signature. */

    /* 0x00/0xFF=ISA, 0x01=PnP, 0x10=VLB, 0x11=PCI */
    if (PCNET_IS_PCI(pThis))
        pThis->aPROM[ 9] = 0x11;
    else
        pThis->aPROM[ 9] = 0x00;

    for (i = 0, checksum = 0; i < 16; i++)
        checksum += pThis->aPROM[i];
    *(uint16_t *)&pThis->aPROM[12] = RT_H2LE_U16(checksum);

    /* Many of the BCR values would normally be read from the EEPROM. */
    pThis->aBCR[BCR_MSRDA] = 0x0005;
    pThis->aBCR[BCR_MSWRA] = 0x0005;
    pThis->aBCR[BCR_MC   ] = 0x0002;
    pThis->aBCR[BCR_LNKST] = 0x00c0;
    pThis->aBCR[BCR_LED1 ] = 0x0084;
    pThis->aBCR[BCR_LED2 ] = 0x0088;
    pThis->aBCR[BCR_LED3 ] = 0x0090;
    /* For ISA PnP cards, BCR8 reports IRQ/DMA (e.g. 0x0035 means IRQ 3, DMA 5). */
    pThis->aBCR[BCR_FDC  ] = 0x0000;
    pThis->aBCR[BCR_BSBC ] = 0x9001;
    pThis->aBCR[BCR_EECAS] = 0x0002;
    pThis->aBCR[BCR_STVAL] = 0xffff;
    pThis->aCSR[58       ] = /* CSR58 is an alias for BCR20 */
    pThis->aBCR[BCR_SWS  ] = 0x0200;
    pThis->iLog2DescSize   = 3;
    pThis->aBCR[BCR_PLAT ] = 0xff06;
    pThis->aBCR[BCR_MIICAS  ] = 0x20;   /* Auto-negotiation on. */
    pThis->aBCR[BCR_MIIADDR ] = 0;  /* Internal PHY on Am79C973 would be (0x1e << 5) */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    pThis->aBCR[BCR_PCIVID] = PCIDevGetVendorId(pPciDev);
    pThis->aBCR[BCR_PCISID] = PCIDevGetSubSystemId(pPciDev);
    pThis->aBCR[BCR_PCISVID] = PCIDevGetSubSystemVendorId(pPciDev);

    /* Reset the error counter. */
    pThis->uCntBadRMD      = 0;

    pcnetSoftReset(pThis);
}
#endif /* IN_RING3 */


/* -=-=-=-=-=- APROM I/O Port access -=-=-=-=-=- */

static void pcnetAPROMWriteU8(PPCNETSTATE pThis, uint32_t addr, uint32_t val)
{
    addr &= 0x0f;
    val  &= 0xff;
    Log(("#%d pcnetAPROMWriteU8: addr=%#010x val=%#04x\n", PCNET_INST_NR, addr, val));
    /* Check APROMWE bit to enable write access */
    if (pcnetBCRReadU16(pThis, 2) & 0x80)
        pThis->aPROM[addr] = val;
}

static uint32_t pcnetAPROMReadU8(PPCNETSTATE pThis, uint32_t addr)
{
    uint32_t val = pThis->aPROM[addr &= 0x0f];
    Log(("#%d pcnetAPROMReadU8: addr=%#010x val=%#04x\n", PCNET_INST_NR, addr, val));
    return val;
}

/**
 * @callback_method_impl{FNIOMIOPORTIN, APROM}
 */
static DECLCALLBACK(VBOXSTRICTRC)
pcnetIOPortAPromRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PPCNETSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    VBOXSTRICTRC    rc    = VINF_SUCCESS;
    STAM_PROFILE_ADV_START(&pThis->StatAPROMRead, a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    /* FreeBSD is accessing in dwords. */
    if (cb == 1)
        *pu32 = pcnetAPROMReadU8(pThis, offPort);
    else if (cb == 2 && !BCR_DWIO(pThis))
        *pu32 = pcnetAPROMReadU8(pThis, offPort)
              | (pcnetAPROMReadU8(pThis, offPort + 1) << 8);
    else if (cb == 4 && BCR_DWIO(pThis))
        *pu32 = pcnetAPROMReadU8(pThis, offPort)
              | (pcnetAPROMReadU8(pThis, offPort + 1) << 8)
              | (pcnetAPROMReadU8(pThis, offPort + 2) << 16)
              | (pcnetAPROMReadU8(pThis, offPort + 3) << 24);
    else
    {
        Log(("#%d pcnetIOPortAPromRead: offPort=%RTiop cb=%d BCR_DWIO !!\n", PCNET_INST_NR, offPort, cb));
        rc = VERR_IOM_IOPORT_UNUSED;
    }

    STAM_PROFILE_ADV_STOP(&pThis->StatAPROMRead, a);
    LogFlow(("#%d pcnetIOPortAPromRead: offPort=%RTiop *pu32=%#RX32 cb=%d rc=%Rrc\n",
             PCNET_INST_NR, offPort, *pu32, cb, VBOXSTRICTRC_VAL(rc)));
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTOUT, APROM}
 */
static DECLCALLBACK(VBOXSTRICTRC)
pcnetIoPortAPromWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PPCNETSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    VBOXSTRICTRC    rc    = VINF_SUCCESS;
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    if (cb == 1)
    {
        STAM_PROFILE_ADV_START(&pThis->StatAPROMWrite, a);
        pcnetAPROMWriteU8(pThis, offPort, u32);
        STAM_PROFILE_ADV_STOP(&pThis->StatAPROMWrite, a);
    }
    else
        rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "offPort=%#x cb=%d u32=%#x\n", offPort, cb, u32);

    LogFlow(("#%d pcnetIoPortAPromWrite: offPort=%RTiop u32=%#RX32 cb=%d rc=%Rrc\n",
             PCNET_INST_NR, offPort, u32, cb, VBOXSTRICTRC_VAL(rc)));
    return rc;
}


/* -=-=-=-=-=- I/O Port access -=-=-=-=-=- */


static VBOXSTRICTRC pcnetIoPortWriteU8(PPCNETSTATE pThis, uint32_t addr, uint32_t val)
{
    RT_NOREF1(val);
    Log6(("#%d pcnetIoPortWriteU8: addr=%#010x val=%#06x\n", PCNET_INST_NR, addr, val));
    if (RT_LIKELY(!BCR_DWIO(pThis)))
    {
        switch (addr & 0x0f)
        {
            case 0x04: /* RESET */
                break;
        }
    }
    else
        Log(("#%d pcnetIoPortWriteU8: addr=%#010x val=%#06x BCR_DWIO !!\n", PCNET_INST_NR, addr, val));

    return VINF_SUCCESS;
}

static VBOXSTRICTRC pcnetIoPortReadU8(PPDMDEVINS pDevIns, PPCNETSTATE pThis, uint32_t addr, uint32_t *val)
{
    *val = UINT32_MAX;

    if (RT_LIKELY(!BCR_DWIO(pThis)))
    {
        switch (addr & 0x0f)
        {
            case 0x04: /* RESET */
                pcnetSoftReset(pThis);
                *val = 0;
                break;
        }
    }
    else
        Log(("#%d pcnetIoPortReadU8: addr=%#010x val=%#06x BCR_DWIO !!\n", PCNET_INST_NR, addr, *val & 0xff));

    pcnetUpdateIrq(pDevIns, pThis);

    Log6(("#%d pcnetIoPortReadU8: addr=%#010x val=%#06x\n", PCNET_INST_NR, addr, *val & 0xff));
    return VINF_SUCCESS;
}

static VBOXSTRICTRC pcnetIoPortWriteU16(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, uint32_t addr, uint32_t val)
{
    VBOXSTRICTRC rc = VINF_SUCCESS;

    Log6(("#%d pcnetIoPortWriteU16: addr=%#010x val=%#06x\n", PCNET_INST_NR, addr, val));
    if (RT_LIKELY(!BCR_DWIO(pThis)))
    {
        switch (addr & 0x0f)
        {
            case 0x00: /* RDP */
                pcnetPollTimer(pDevIns, pThis, pThisCC);
                rc = pcnetCSRWriteU16(pDevIns, pThis, pThisCC, pThis->u32RAP, val);
                pcnetUpdateIrq(pDevIns, pThis);
                break;
            case 0x02: /* RAP */
                pThis->u32RAP = val & 0x7f;
                break;
            case 0x06: /* BDP */
                rc = pcnetBCRWriteU16(pDevIns, pThis, pThis->u32RAP, val);
                break;
        }
    }
    else
        Log(("#%d pcnetIoPortWriteU16: addr=%#010x val=%#06x BCR_DWIO !!\n", PCNET_INST_NR, addr, val));

    return rc;
}

static VBOXSTRICTRC pcnetIoPortReadU16(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, uint32_t addr, uint32_t *val)
{
    VBOXSTRICTRC rc = VINF_SUCCESS;

    *val = ~0U;

    if (RT_LIKELY(!BCR_DWIO(pThis)))
    {
        switch (addr & 0x0f)
        {
            case 0x00: /* RDP */
                /** @note if we're not polling, then the guest will tell us when to poll by setting TDMD in CSR0 */
                /** Polling is then useless here and possibly expensive. */
                if (!CSR_DPOLL(pThis))
                    pcnetPollTimer(pDevIns, pThis, pThisCC);

                rc = pcnetCSRReadU16(pDevIns, pThis, pThisCC, pThis->u32RAP, val);
                if (pThis->u32RAP == 0)  // pcnetUpdateIrq() already called by pcnetCSRReadU16()
                    goto skip_update_irq;
                break;
            case 0x02: /* RAP */
                *val = pThis->u32RAP;
                goto skip_update_irq;
            case 0x04: /* RESET */
                pcnetSoftReset(pThis);
                *val = 0;
                break;
            case 0x06: /* BDP */
                *val = pcnetBCRReadU16(pThis, pThis->u32RAP);
                break;
        }
    }
    else
        Log(("#%d pcnetIoPortReadU16: addr=%#010x val=%#06x BCR_DWIO !!\n", PCNET_INST_NR, addr, *val & 0xffff));

    pcnetUpdateIrq(pDevIns, pThis);

skip_update_irq:
    Log6(("#%d pcnetIoPortReadU16: addr=%#010x val=%#06x\n", PCNET_INST_NR, addr, *val & 0xffff));
    return rc;
}

static VBOXSTRICTRC pcnetIoPortWriteU32(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, uint32_t addr, uint32_t val)
{
    VBOXSTRICTRC rc = VINF_SUCCESS;

    Log6(("#%d pcnetIoPortWriteU32: addr=%#010x val=%#010x\n", PCNET_INST_NR, addr, val));
    if (RT_LIKELY(BCR_DWIO(pThis)))
    {
        switch (addr & 0x0f)
        {
            case 0x00: /* RDP */
                pcnetPollTimer(pDevIns, pThis, pThisCC);
                rc = pcnetCSRWriteU16(pDevIns, pThis, pThisCC, pThis->u32RAP, val & 0xffff);
                pcnetUpdateIrq(pDevIns, pThis);
                break;
            case 0x04: /* RAP */
                pThis->u32RAP = val & 0x7f;
                break;
            case 0x0c: /* BDP */
                rc = pcnetBCRWriteU16(pDevIns, pThis, pThis->u32RAP, val & 0xffff);
                break;
        }
    }
    else if ((addr & 0x0f) == 0)
    {
        /* switch device to dword I/O mode */
        pcnetBCRWriteU16(pDevIns, pThis, BCR_BSBC, pcnetBCRReadU16(pThis, BCR_BSBC) | 0x0080);
        Log6(("device switched into dword i/o mode\n"));
    }
    else
        Log(("#%d pcnetIoPortWriteU32: addr=%#010x val=%#010x !BCR_DWIO !!\n", PCNET_INST_NR, addr, val));

    return rc;
}

static VBOXSTRICTRC pcnetIoPortReadU32(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, uint32_t addr, uint32_t *val)
{
    VBOXSTRICTRC rc = VINF_SUCCESS;

    *val = ~0U;

    if (RT_LIKELY(BCR_DWIO(pThis)))
    {
        switch (addr & 0x0f)
        {
            case 0x00: /* RDP */
                /** @note if we're not polling, then the guest will tell us when to poll by setting TDMD in CSR0 */
                /** Polling is then useless here and possibly expensive. */
                if (!CSR_DPOLL(pThis))
                    pcnetPollTimer(pDevIns, pThis, pThisCC);

                rc = pcnetCSRReadU16(pDevIns, pThis, pThisCC, pThis->u32RAP, val);
                if (pThis->u32RAP == 0)  // pcnetUpdateIrq() already called by pcnetCSRReadU16()
                    goto skip_update_irq;
                break;
            case 0x04: /* RAP */
                *val = pThis->u32RAP;
                goto skip_update_irq;
            case 0x08: /* RESET */
                pcnetSoftReset(pThis);
                *val = 0;
                break;
            case 0x0c: /* BDP */
                *val = pcnetBCRReadU16(pThis, pThis->u32RAP);
                break;
        }
    }
    else
        Log(("#%d pcnetIoPortReadU32: addr=%#010x val=%#010x !BCR_DWIO !!\n", PCNET_INST_NR, addr, *val));
    pcnetUpdateIrq(pDevIns, pThis);

skip_update_irq:
    Log6(("#%d pcnetIoPortReadU32: addr=%#010x val=%#010x\n", PCNET_INST_NR, addr, *val));
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) pcnetIoPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PPCNETSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    VBOXSTRICTRC    rc;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIORead), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1: rc = pcnetIoPortReadU8(pDevIns, pThis, offPort, pu32); break;
        case 2: rc = pcnetIoPortReadU16(pDevIns, pThis, pThisCC, offPort, pu32); break;
        case 4: rc = pcnetIoPortReadU32(pDevIns, pThis, pThisCC, offPort, pu32); break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "pcnetIoPortRead: unsupported op size: offset=%#10x cb=%u\n", offPort, cb);
    }

    Log2(("#%d pcnetIoPortRead: offPort=%RTiop *pu32=%#RX32 cb=%d rc=%Rrc\n", PCNET_INST_NR, offPort, *pu32, cb, VBOXSTRICTRC_VAL(rc)));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIORead), a);
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) pcnetIoPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PPCNETSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    VBOXSTRICTRC    rc;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1: rc = pcnetIoPortWriteU8(pThis, offPort, u32); break;
        case 2: rc = pcnetIoPortWriteU16(pDevIns, pThis, pThisCC, offPort, u32); break;
        case 4: rc = pcnetIoPortWriteU32(pDevIns, pThis, pThisCC, offPort, u32); break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "pcnetIoPortWrite: unsupported op size: offset=%#10x cb=%u\n", offPort, cb);
    }

    Log2(("#%d pcnetIoPortWrite: offPort=%RTiop u32=%#RX32 cb=%d rc=%Rrc\n", PCNET_INST_NR, offPort, u32, cb, VBOXSTRICTRC_VAL(rc)));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    return rc;
}


/* -=-=-=-=-=- MMIO -=-=-=-=-=- */

#ifdef IN_RING3

static void pcnetR3MmioWriteU8(PPCNETSTATE pThis, RTGCPHYS off, uint32_t val)
{
    Log6(("#%d pcnetR3MmioWriteU8: off=%#010x val=%#04x\n", PCNET_INST_NR, off, val));
    if (!(off & 0x10))
        pcnetAPROMWriteU8(pThis, off, val);
}

static VBOXSTRICTRC pcnetR3MmioReadU8(PPCNETSTATE pThis, RTGCPHYS addr, uint8_t *val)
{
    *val = 0xff;
    if (!(addr & 0x10))
        *val = pcnetAPROMReadU8(pThis, addr);
    Log6(("#%d pcnetR3MmioReadU8: addr=%#010x val=%#04x\n", PCNET_INST_NR, addr, *val));
    return VINF_SUCCESS;
}

static VBOXSTRICTRC pcnetR3MmioWriteU16(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, RTGCPHYS off, uint32_t val)
{
    VBOXSTRICTRC rcStrict;
    Log6(("#%d pcnetR3MmioWriteU16: off=%#010x val=%#06x\n", PCNET_INST_NR, off, val));
    if (off & 0x10)
    {
        rcStrict = pcnetIoPortWriteU16(pDevIns, pThis, pThisCC, off & 0x0f, val);
        if (rcStrict == VINF_IOM_R3_IOPORT_WRITE)
            rcStrict = VINF_IOM_R3_MMIO_WRITE;
    }
    else
    {
        pcnetAPROMWriteU8(pThis, off,   val     );
        pcnetAPROMWriteU8(pThis, off + 1, val >> 8);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}

static VBOXSTRICTRC pcnetR3MmioReadU16(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, RTGCPHYS addr, uint16_t *val)
{
    VBOXSTRICTRC rcStrict;
    uint32_t val32 = ~0U;

    if (addr & 0x10)
    {
        rcStrict = pcnetIoPortReadU16(pDevIns, pThis, pThisCC, addr & 0x0f, &val32);
        if (rcStrict == VINF_IOM_R3_IOPORT_READ)
            rcStrict = VINF_IOM_R3_MMIO_READ;
    }
    else
    {
        val32 = pcnetAPROMReadU8(pThis, addr+1);
        val32 <<= 8;
        val32 |= pcnetAPROMReadU8(pThis, addr);
        rcStrict = VINF_SUCCESS;
    }
    *val = (uint16_t)val32;
    Log6(("#%d pcnetR3MmioReadU16: addr=%#010x val = %#06x\n", PCNET_INST_NR, addr, *val));
    return rcStrict;
}

static VBOXSTRICTRC pcnetR3MmioWriteU32(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, RTGCPHYS off, uint32_t val)
{
    VBOXSTRICTRC rcStrict;
    Log6(("#%d pcnetR3MmioWriteU32: off=%#010x val=%#010x\n", PCNET_INST_NR, off, val));
    if (off & 0x10)
    {
        rcStrict = pcnetIoPortWriteU32(pDevIns, pThis, pThisCC, off & 0x0f, val);
        if (rcStrict == VINF_IOM_R3_IOPORT_WRITE)
            rcStrict = VINF_IOM_R3_MMIO_WRITE;
    }
    else
    {
        pcnetAPROMWriteU8(pThis, off,     val      );
        pcnetAPROMWriteU8(pThis, off + 1, val >>  8);
        pcnetAPROMWriteU8(pThis, off + 2, val >> 16);
        pcnetAPROMWriteU8(pThis, off + 3, val >> 24);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}

static VBOXSTRICTRC pcnetR3MmioReadU32(PPDMDEVINS pDevIns, PPCNETSTATE pThis, PPCNETSTATECC pThisCC, RTGCPHYS addr, uint32_t *val)
{
    VBOXSTRICTRC rcStrict;

    if (addr & 0x10)
    {
        rcStrict = pcnetIoPortReadU32(pDevIns, pThis, pThisCC, addr & 0x0f, val);
        if (rcStrict == VINF_IOM_R3_IOPORT_READ)
            rcStrict = VINF_IOM_R3_MMIO_READ;
    }
    else
    {
        uint32_t    val32;

        val32  = pcnetAPROMReadU8(pThis, addr+3);
        val32 <<= 8;
        val32 |= pcnetAPROMReadU8(pThis, addr+2);
        val32 <<= 8;
        val32 |= pcnetAPROMReadU8(pThis, addr+1);
        val32 <<= 8;
        val32 |= pcnetAPROMReadU8(pThis, addr  );
        *val = val32;
        rcStrict = VINF_SUCCESS;
    }
    Log6(("#%d pcnetR3MmioReadU32: addr=%#010x val=%#010x\n", PCNET_INST_NR, addr, *val));
    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) pcnetR3MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PPCNETSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    VBOXSTRICTRC    rc      = VINF_SUCCESS;
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    /*
     * We have to check the range, because we're page aligning the MMIO.
     */
    if (off < PCNET_PNPMMIO_SIZE)
    {
        STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatMMIORead), a);
        switch (cb)
        {
            case 1:  rc = pcnetR3MmioReadU8 (pThis, off, (uint8_t *)pv); break;
            case 2:  rc = pcnetR3MmioReadU16(pDevIns, pThis, pThisCC, off, (uint16_t *)pv); break;
            case 4:  rc = pcnetR3MmioReadU32(pDevIns, pThis, pThisCC, off, (uint32_t *)pv); break;
            default:
                memset(pv, 0, cb);
                rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "pcnetR3MmioRead: unsupported op size: address=%RGp cb=%u\n", off, cb);
                break;
        }
        STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatMMIORead), a);
    }
    else
        memset(pv, 0, cb);

    LogFlow(("#%d pcnetR3MmioRead: pvUser=%p:{%.*Rhxs} cb=%d off=%RGp rc=%Rrc\n",
             PCNET_INST_NR, pv, cb, pv, cb, off, VBOXSTRICTRC_VAL(rc)));
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) pcnetR3MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PPCNETSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    VBOXSTRICTRC    rc      = VINF_SUCCESS;
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    /*
     * We have to check the range, because we're page aligning the MMIO stuff presently.
     */
    if (off < PCNET_PNPMMIO_SIZE)
    {
        STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatMMIOWrite), a);
        switch (cb)
        {
            case 1:  pcnetR3MmioWriteU8(pThis, off, *(uint8_t  *)pv); break;
            case 2:  rc = pcnetR3MmioWriteU16(pDevIns, pThis, pThisCC, off, *(uint16_t *)pv); break;
            case 4:  rc = pcnetR3MmioWriteU32(pDevIns, pThis, pThisCC, off, *(uint32_t *)pv); break;
            default:
                rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "pcnetR3MmioWrite: unsupported op size: address=%RGp cb=%u\n", off, cb);
        }

        STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatMMIOWrite), a);
    }
    LogFlow(("#%d pcnetR3MmioWrite: pvUser=%p:{%.*Rhxs} cb=%d off=%RGp rc=%Rrc\n",
             PCNET_INST_NR, pv, cb, pv, cb, off, VBOXSTRICTRC_VAL(rc)));
    return rc;
}



/* -=-=-=-=-=- Timer Callbacks -=-=-=-=-=- */

/**
 * @callback_method_impl{FNTMTIMERDEV, Poll timer}
 */
static DECLCALLBACK(void) pcnetR3Timer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPCNETSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    Assert(hTimer == pThis->hTimerPoll); RT_NOREF(pvUser, hTimer);

    STAM_PROFILE_ADV_START(&pThis->StatTimer, a);
    pcnetPollTimer(pDevIns, pThis, pThisCC);
    STAM_PROFILE_ADV_STOP(&pThis->StatTimer, a);
}


/**
 * @callback_method_impl{FNTMTIMERDEV,
 *      Software interrupt timer callback function.}
 */
static DECLCALLBACK(void) pcnetR3TimerSoftInt(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    Assert(hTimer == pThis->hTimerSoftInt); RT_NOREF(pvUser, hTimer);

    pThis->aCSR[7] |= 0x0800; /* STINT */
    pcnetUpdateIrq(pDevIns, pThis);
    PDMDevHlpTimerSetNano(pDevIns, pThis->hTimerSoftInt, 12800U * (pThis->aBCR[BCR_STVAL] & 0xffff));
}


/**
 * @callback_method_impl{FNTMTIMERDEV, Restore timer callback}
 *
 * This is only called when we restore a saved state and temporarily
 * disconnected the network link to inform the guest that network connections
 * should be considered lost.
 */
static DECLCALLBACK(void) pcnetR3TimerRestore(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    Assert(hTimer == pThis->hTimerRestore); RT_NOREF(pvUser);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rc);

    rc = VERR_GENERAL_FAILURE;

    /* 10 Mbps models (up to and including Am79C970A) have no MII and no way to get
     * an MII management auto-poll interrupt (MAPINT) indicating link state changes.
     * In some cases we want to make sure the guest really noticed the link going down;
     * the cLinkDownReported counter is incremented every time the guest did something
     * that might have made it notice the link loss, and we only bring the link back
     * up once we're reasonably certain the guest knows it was down.
     */
    if (pThis->cLinkDownReported <= PCNET_MAX_LINKDOWN_REPORTED)
    {
        rc = PDMDevHlpTimerSetMillies(pDevIns, hTimer, 1500);
        AssertRC(rc);
    }
    if (RT_FAILURE(rc))
    {
        pThis->fLinkTempDown = false;
        if (pThis->fLinkUp)
        {
            LogRel(("PCnet#%d: The link is back up again after the restore.\n", pDevIns->iInstance));
            Log(("#%d pcnetR3TimerRestore: Clearing ERR and CERR after load. cLinkDownReported=%d\n",
                 pDevIns->iInstance, pThis->cLinkDownReported));
            pThis->aCSR[0] &= ~(RT_BIT(15) | RT_BIT(13)); /* ERR | CERR - probably not 100% correct either... */
            pThis->Led.Actual.s.fError = 0;
        }
    }
    else
        Log(("#%d pcnetR3TimerRestore: cLinkDownReported=%d, wait another 1500ms...\n",
             pDevIns->iInstance, pThis->cLinkDownReported));

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/* -=-=-=-=-=- PCI Device Callbacks -=-=-=-=-=- */

/**
 * @callback_method_impl{FNPCIIOREGIONMAP, For the PCnet I/O Ports.}
 */
static DECLCALLBACK(int) pcnetR3PciMapUnmapIoPorts(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                   RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    int         rc;
    RT_NOREF(iRegion, cb, enmType, pPciDev);

    Assert(pDevIns->apPciDevs[0] == pPciDev);
    Assert(enmType == PCI_ADDRESS_SPACE_IO);
    Assert(cb >= 0x20);

    if (GCPhysAddress != NIL_RTGCPHYS)
    {
        RTIOPORT Port = (RTIOPORT)GCPhysAddress;
        rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortsPciAProm, Port);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortsPci, Port + 0x10);
        AssertRCReturn(rc, rc);
        pThis->IOPortBase = Port;
    }
    else
    {
        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortsPciAProm);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortsPci);
        AssertRCReturn(rc, rc);
        pThis->IOPortBase = 0;
    }

    return VINF_SUCCESS;
}


/* -=-=-=-=-=- Debug Info Handler -=-=-=-=-=- */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) pcnetR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    bool        fRcvRing = false;
    bool        fXmtRing = false;
    bool        fAPROM   = false;

    /*
     * Parse args.
     */
    if (pszArgs)
    {
        fRcvRing = strstr(pszArgs, "verbose") || strstr(pszArgs, "rcv");
        fXmtRing = strstr(pszArgs, "verbose") || strstr(pszArgs, "xmt");
        fAPROM   = strstr(pszArgs, "verbose") || strstr(pszArgs, "aprom");
    }

    /*
     * Show info.
     */
    const char *pcszModel;
    switch (pThis->uDevType)
    {
        case DEV_AM79C970A:     pcszModel = "AM79C970A";                break;
        case DEV_AM79C973:      pcszModel = "AM79C973";                 break;
        case DEV_AM79C960:      pcszModel = "AM79C960/NE2100";          break;
        case DEV_AM79C960_EB:   pcszModel = "AM79C960/EtherBlaster";    break;
        default:                pcszModel = "Unknown";                  break;
    }
    pHlp->pfnPrintf(pHlp,
                    "pcnet #%d: port=%RTiop", pDevIns->iInstance, pThis->IOPortBase);
    if (PCNET_IS_ISA(pThis))
        pHlp->pfnPrintf(pHlp, " irq=%RX32", pThis->uIsaIrq);
    else
        pHlp->pfnPrintf(pHlp, " mmio=%RX32", PDMDevHlpMmioGetMappingAddress(pDevIns, pThis->hMmioPci));

    pHlp->pfnPrintf(pHlp,
                    " mac-cfg=%RTmac %s%s%s\n",
                    &pThis->MacConfigured, pcszModel, pDevIns->fRCEnabled ? " RC" : "", pDevIns->fR0Enabled ? " R0" : "");


    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_INTERNAL_ERROR); /* Take it here so we know why we're hanging... */
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    pHlp->pfnPrintf(pHlp,
                    "CSR0=%#06x: INIT=%d STRT=%d STOP=%d TDMD=%d TXON=%d RXON=%d IENA=%d INTR=%d IDON=%d TINT=%d RINT=%d MERR=%d\n"
                    "              MISS=%d CERR=%d BABL=%d ERR=%d\n",
                    pThis->aCSR[0],
                    !!(pThis->aCSR[0] & RT_BIT(0)), !!(pThis->aCSR[0] & RT_BIT(1)), !!(pThis->aCSR[0] & RT_BIT(2)), !!(pThis->aCSR[0] & RT_BIT(3)), !!(pThis->aCSR[0] & RT_BIT(4)),
                    !!(pThis->aCSR[0] & RT_BIT(5)), !!(pThis->aCSR[0] & RT_BIT(6)), !!(pThis->aCSR[0] & RT_BIT(7)), !!(pThis->aCSR[0] & RT_BIT(8)), !!(pThis->aCSR[0] & RT_BIT(9)),
                    !!(pThis->aCSR[0] & RT_BIT(10)), !!(pThis->aCSR[0] & RT_BIT(11)), !!(pThis->aCSR[0] & RT_BIT(12)), !!(pThis->aCSR[0] & RT_BIT(13)),
                    !!(pThis->aCSR[0] & RT_BIT(14)), !!(pThis->aCSR[0] & RT_BIT(15)));

    pHlp->pfnPrintf(pHlp,
                    "CSR1=%#06x:\n",
                    pThis->aCSR[1]);

    pHlp->pfnPrintf(pHlp,
                    "CSR2=%#06x:\n",
                    pThis->aCSR[2]);

    pHlp->pfnPrintf(pHlp,
                    "CSR3=%#06x: BSWP=%d EMBA=%d DXMT2PD=%d LAPPEN=%d DXSUFLO=%d IDONM=%d TINTM=%d RINTM=%d MERRM=%d MISSM=%d BABLM=%d\n",
                    pThis->aCSR[3],
                    !!(pThis->aCSR[3] & RT_BIT(2)), !!(pThis->aCSR[3] & RT_BIT(3)), !!(pThis->aCSR[3] & RT_BIT(4)), CSR_LAPPEN(pThis),
                    CSR_DXSUFLO(pThis), !!(pThis->aCSR[3] & RT_BIT(8)), !!(pThis->aCSR[3] & RT_BIT(9)), !!(pThis->aCSR[3] & RT_BIT(10)),
                    !!(pThis->aCSR[3] & RT_BIT(11)), !!(pThis->aCSR[3] & RT_BIT(12)), !!(pThis->aCSR[3] & RT_BIT(14)));

    pHlp->pfnPrintf(pHlp,
                    "CSR4=%#06x: JABM=%d JAB=%d TXSTRM=%d TXSTRT=%d RCVCOOM=%d RCVCCO=%d UINT=%d UINTCMD=%d\n"
                    "              MFCOM=%d MFCO=%d ASTRP_RCV=%d APAD_XMT=%d DPOLL=%d TIMER=%d EMAPLUS=%d EN124=%d\n",
                    pThis->aCSR[4],
                    !!(pThis->aCSR[4] & RT_BIT( 0)), !!(pThis->aCSR[4] & RT_BIT( 1)), !!(pThis->aCSR[4] & RT_BIT( 2)), !!(pThis->aCSR[4] & RT_BIT( 3)),
                    !!(pThis->aCSR[4] & RT_BIT( 4)), !!(pThis->aCSR[4] & RT_BIT( 5)), !!(pThis->aCSR[4] & RT_BIT( 6)), !!(pThis->aCSR[4] & RT_BIT( 7)),
                    !!(pThis->aCSR[4] & RT_BIT( 8)), !!(pThis->aCSR[4] & RT_BIT( 9)), !!(pThis->aCSR[4] & RT_BIT(10)), !!(pThis->aCSR[4] & RT_BIT(11)),
                    !!(pThis->aCSR[4] & RT_BIT(12)), !!(pThis->aCSR[4] & RT_BIT(13)), !!(pThis->aCSR[4] & RT_BIT(14)), !!(pThis->aCSR[4] & RT_BIT(15)));

    pHlp->pfnPrintf(pHlp,
                    "CSR5=%#06x:\n",
                    pThis->aCSR[5]);

    pHlp->pfnPrintf(pHlp,
                    "CSR6=%#06x: RLEN=%#x* TLEN=%#x* [* encoded]\n",
                    pThis->aCSR[6],
                    (pThis->aCSR[6] >> 8) & 0xf, (pThis->aCSR[6] >> 12) & 0xf);

    pHlp->pfnPrintf(pHlp,
                    "CSR8..11=%#06x,%#06x,%#06x,%#06x: LADRF=%#018llx\n",
                    pThis->aCSR[8], pThis->aCSR[9], pThis->aCSR[10], pThis->aCSR[11],
                      (uint64_t)(pThis->aCSR[ 8] & 0xffff)
                    | (uint64_t)(pThis->aCSR[ 9] & 0xffff) << 16
                    | (uint64_t)(pThis->aCSR[10] & 0xffff) << 32
                    | (uint64_t)(pThis->aCSR[11] & 0xffff) << 48);

    pHlp->pfnPrintf(pHlp,
                    "CSR12..14=%#06x,%#06x,%#06x: PADR=%02x:%02x:%02x:%02x:%02x:%02x (Current MAC Address)\n",
                    pThis->aCSR[12], pThis->aCSR[13], pThis->aCSR[14],
                     pThis->aCSR[12]       & 0xff,
                    (pThis->aCSR[12] >> 8) & 0xff,
                     pThis->aCSR[13]       & 0xff,
                    (pThis->aCSR[13] >> 8) & 0xff,
                     pThis->aCSR[14]       & 0xff,
                    (pThis->aCSR[14] >> 8) & 0xff);

    pHlp->pfnPrintf(pHlp,
                    "CSR15=%#06x: DXR=%d DTX=%d LOOP=%d DXMTFCS=%d FCOLL=%d DRTY=%d INTL=%d PORTSEL=%d LTR=%d\n"
                    "              MENDECL=%d DAPC=%d DLNKTST=%d DRCVPV=%d DRCVBC=%d PROM=%d\n",
                    pThis->aCSR[15],
                    !!(pThis->aCSR[15] & RT_BIT( 0)), !!(pThis->aCSR[15] & RT_BIT( 1)), !!(pThis->aCSR[15] & RT_BIT( 2)), !!(pThis->aCSR[15] & RT_BIT( 3)),
                    !!(pThis->aCSR[15] & RT_BIT( 4)), !!(pThis->aCSR[15] & RT_BIT( 5)), !!(pThis->aCSR[15] & RT_BIT( 6)),   (pThis->aCSR[15] >> 7) & 3,
                                                   !!(pThis->aCSR[15] & RT_BIT( 9)), !!(pThis->aCSR[15] & RT_BIT(10)), !!(pThis->aCSR[15] & RT_BIT(11)),
                    !!(pThis->aCSR[15] & RT_BIT(12)), !!(pThis->aCSR[15] & RT_BIT(13)), !!(pThis->aCSR[15] & RT_BIT(14)), !!(pThis->aCSR[15] & RT_BIT(15)));

    pHlp->pfnPrintf(pHlp,
                    "CSR46=%#06x: POLL=%#06x (Poll Time Counter)\n",
                    pThis->aCSR[46], pThis->aCSR[46] & 0xffff);

    pHlp->pfnPrintf(pHlp,
                    "CSR47=%#06x: POLLINT=%#06x (Poll Time Interval)\n",
                    pThis->aCSR[47], pThis->aCSR[47] & 0xffff);

    pHlp->pfnPrintf(pHlp,
                    "CSR58=%#06x: SWSTYLE=%d %s SSIZE32=%d CSRPCNET=%d APERRENT=%d\n",
                    pThis->aCSR[58],
                    pThis->aCSR[58] & 0x7f,
                    (pThis->aCSR[58] & 0x7f) == 0 ? "C-LANCE / PCnet-ISA"
                    : (pThis->aCSR[58] & 0x7f) == 1 ? "ILACC"
                    : (pThis->aCSR[58] & 0x7f) == 2 ? "PCnet-32"
                    : (pThis->aCSR[58] & 0x7f) == 3 ? "PCnet-PCI II"
                    : "!!reserved!!",
                    !!(pThis->aCSR[58] & RT_BIT(8)), !!(pThis->aCSR[58] & RT_BIT(9)), !!(pThis->aCSR[58] & RT_BIT(10)));

    pHlp->pfnPrintf(pHlp,
                    "CSR112=%04RX32: MFC=%04x (Missed receive Frame Count)\n",
                    pThis->aCSR[112], pThis->aCSR[112] & 0xffff);

    pHlp->pfnPrintf(pHlp,
                    "CSR122=%04RX32: RCVALGN=%04x (Receive Frame Align)\n",
                    pThis->aCSR[122], !!(pThis->aCSR[122] & RT_BIT(0)));

    pHlp->pfnPrintf(pHlp,
                    "CSR124=%04RX32: RPA=%04x (Runt Packet Accept)\n",
                    pThis->aCSR[122], !!(pThis->aCSR[122] & RT_BIT(3)));

    if (pThis->uDevType == DEV_AM79C970A || pThis->uDevType == DEV_AM79C973)
    {
        /* Print the Burst and Bus Control Register; the DWIO bit is quite important. */
        pHlp->pfnPrintf(pHlp,
                        "BCR18=%#04x: ROMTMG=%u MEMCMD=%u EXTREQ=%u\n"
                        "              DWIO=%u BREADE=%u BWRITE=%u\n",
                        pThis->aBCR[18],
                        (pThis->aBCR[18] >> 12) & 0xf, !!(pThis->aBCR[18] & RT_BIT(9)), !!(pThis->aBCR[18] & RT_BIT(8)),
                        !!(pThis->aBCR[18] & RT_BIT(7)), !!(pThis->aBCR[18] & RT_BIT(6)), !!(pThis->aBCR[18] & RT_BIT(5)));
    }

    if (pThis->uDevType == DEV_AM79C973)
    {
        /* Print a bit of the MII state. */
        pHlp->pfnPrintf(pHlp,
                        "BCR32=%#06x: MIIILP=%d XPHYSP=%d XPHYFD=%d XPHYANE=%d XPHYRST=%d\n"
                        "              DANAS=%d APDW=%u APEP=%d FMDC=%u MIIPD=%d ANTST=%d\n",
                        pThis->aBCR[32],
                        !!(pThis->aBCR[32] & RT_BIT( 1)), !!(pThis->aBCR[32] & RT_BIT( 3)), !!(pThis->aBCR[32] & RT_BIT( 4)),
                        !!(pThis->aBCR[32] & RT_BIT( 5)), !!(pThis->aBCR[32] & RT_BIT( 6)), !!(pThis->aBCR[32] & RT_BIT( 7)),
                        (pThis->aBCR[32] >> 8) & 0x7,
                        !!(pThis->aBCR[32] & RT_BIT(11)),
                        (pThis->aBCR[32] >> 12) & 0x3,
                        !!(pThis->aBCR[32] & RT_BIT(14)), !!(pThis->aBCR[32] & RT_BIT(15)));
    }

    /*
     * Dump the receive ring.
     */
    pHlp->pfnPrintf(pHlp,
                    "RCVRL=%04x RCVRC=%04x  GCRDRA=%RX32 \n"
                    "CRDA=%08RX32 CRBA=%08RX32 CRBC=%03x CRST=%04x\n"
                    "NRDA=%08RX32 NRBA=%08RX32 NRBC=%03x NRST=%04x\n"
                    "NNRDA=%08RX32\n"
                    ,
                    CSR_RCVRL(pThis), CSR_RCVRC(pThis), pThis->GCRDRA,
                    CSR_CRDA(pThis), CSR_CRBA(pThis), CSR_CRBC(pThis), CSR_CRST(pThis),
                    CSR_NRDA(pThis), CSR_NRBA(pThis), CSR_NRBC(pThis), CSR_NRST(pThis),
                    CSR_NNRD(pThis));
    if (fRcvRing)
    {
        const unsigned  cb = 1 << pThis->iLog2DescSize;
        RTGCPHYS32      GCPhys = pThis->GCRDRA;
        unsigned        i = CSR_RCVRL(pThis);
        while (i-- > 0)
        {
            RMD rmd;
            pcnetRmdLoad(pDevIns, pThis, &rmd, PHYSADDR(pThis, GCPhys), false);
            pHlp->pfnPrintf(pHlp,
                            "%04x %RX32:%c%c RBADR=%08RX32 BCNT=%03x MCNT=%03x "
                            "OWN=%d ERR=%d FRAM=%d OFLO=%d CRC=%d BUFF=%d STP=%d ENP=%d BPE=%d "
                            "PAM=%d LAFM=%d BAM=%d RCC=%02x RPC=%02x ONES=%#x ZEROS=%d\n",
                            i, GCPhys, i + 1 == CSR_RCVRC(pThis) ? '*' : ' ', GCPhys == CSR_CRDA(pThis) ? '*' : ' ',
                            rmd.rmd0.rbadr, 4096 - rmd.rmd1.bcnt, rmd.rmd2.mcnt,
                            rmd.rmd1.own, rmd.rmd1.err, rmd.rmd1.fram, rmd.rmd1.oflo, rmd.rmd1.crc, rmd.rmd1.buff,
                            rmd.rmd1.stp, rmd.rmd1.enp, rmd.rmd1.bpe,
                            rmd.rmd1.pam, rmd.rmd1.lafm, rmd.rmd1.bam, rmd.rmd2.rcc, rmd.rmd2.rpc,
                            rmd.rmd1.ones, rmd.rmd2.zeros);

            GCPhys += cb;
        }
    }

    /*
     * Dump the transmit ring.
     */
    pHlp->pfnPrintf(pHlp,
                    "XMTRL=%04x XMTRC=%04x  GCTDRA=%08RX32 BADX=%08RX32\n"
                    "PXDA=%08RX32               PXBC=%03x PXST=%04x\n"
                    "CXDA=%08RX32 CXBA=%08RX32 CXBC=%03x CXST=%04x\n"
                    "NXDA=%08RX32 NXBA=%08RX32 NXBC=%03x NXST=%04x\n"
                    "NNXDA=%08RX32\n"
                    ,
                    CSR_XMTRL(pThis), CSR_XMTRC(pThis),
                    pThis->GCTDRA, CSR_BADX(pThis),
                    CSR_PXDA(pThis),                  CSR_PXBC(pThis), CSR_PXST(pThis),
                    CSR_CXDA(pThis), CSR_CXBA(pThis), CSR_CXBC(pThis), CSR_CXST(pThis),
                    CSR_NXDA(pThis), CSR_NXBA(pThis), CSR_NXBC(pThis), CSR_NXST(pThis),
                    CSR_NNXD(pThis));
    if (fXmtRing)
    {
        const unsigned  cb = 1 << pThis->iLog2DescSize;
        RTGCPHYS32      GCPhys = pThis->GCTDRA;
        unsigned        i = CSR_XMTRL(pThis);
        while (i-- > 0)
        {
            TMD tmd;
            pcnetTmdLoadAll(pDevIns, pThis, &tmd, PHYSADDR(pThis, GCPhys));
            pHlp->pfnPrintf(pHlp,
                            "%04x %RX32:%c%c TBADR=%08RX32 BCNT=%03x OWN=%d "
                            "ERR=%d NOFCS=%d LTINT=%d ONE=%d DEF=%d STP=%d ENP=%d BPE=%d "
                            "BUFF=%d UFLO=%d EXDEF=%d LCOL=%d LCAR=%d RTRY=%d TDR=%03x TRC=%#x ONES=%#x\n"
                            ,
                            i,
                            GCPhys,
                            i + 1 == CSR_XMTRC(pThis) ? '*' : ' ',
                            GCPhys == CSR_CXDA(pThis) ? '*' : ' ',
                            tmd.tmd0.tbadr,
                            4096 - tmd.tmd1.bcnt,
                            tmd.tmd1.own,
                            tmd.tmd1.err,
                            tmd.tmd1.nofcs,
                            tmd.tmd1.ltint,
                            tmd.tmd1.one,
                            tmd.tmd1.def,
                            tmd.tmd1.stp,
                            tmd.tmd1.enp,
                            tmd.tmd1.bpe,
                            tmd.tmd2.buff,
                            tmd.tmd2.uflo,
                            tmd.tmd2.exdef,
                            tmd.tmd2.lcol,
                            tmd.tmd2.lcar,
                            tmd.tmd2.rtry,
                            tmd.tmd2.tdr,
                            tmd.tmd2.trc,
                            tmd.tmd1.ones);

            GCPhys += cb;
        }
    }

    /* Dump the Addres PROM (APROM). */
    if (fAPROM)
    {
        pHlp->pfnPrintf(pHlp,
                        "Address PROM:\n  %Rhxs\n", pThis->aPROM);


    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/* -=-=-=-=-=- Helper(s) -=-=-=-=-=- */

/**
 * Takes down the link temporarily if it's current status is up.
 *
 * This is used during restore and when replumbing the network link.
 *
 * The temporary link outage is supposed to indicate to the OS that all network
 * connections have been lost and that it for instance is appropriate to
 * renegotiate any DHCP lease.
 *
 * @param  pThis        The PCnet shared instance data.
 */
static void pcnetR3TempLinkDown(PPDMDEVINS pDevIns, PPCNETSTATE pThis)
{
    if (pThis->fLinkUp)
    {
        pThis->fLinkTempDown = true;
        pThis->cLinkDownReported = 0;
        pThis->aCSR[0] |= RT_BIT(15) | RT_BIT(13); /* ERR | CERR (this is probably wrong) */
        pThis->Led.Asserted.s.fError = pThis->Led.Actual.s.fError = 1;
        int rc = PDMDevHlpTimerSetMillies(pDevIns, pThis->hTimerRestore, pThis->cMsLinkUpDelay);
        AssertRC(rc);
    }
}


/* -=-=-=-=-=- Saved State -=-=-=-=-=- */

/**
 * Saves the configuration.
 *
 * @param   pHlp        The device helpers.
 * @param   pThis       The PCnet shared instance data.
 * @param   pSSM        The saved state handle.
 */
static void pcnetR3SaveConfig(PCPDMDEVHLPR3 pHlp, PPCNETSTATE pThis, PSSMHANDLE pSSM)
{
    pHlp->pfnSSMPutMem(pSSM, &pThis->MacConfigured, sizeof(pThis->MacConfigured));
    pHlp->pfnSSMPutU8(pSSM, pThis->uDevType);
    pHlp->pfnSSMPutU32(pSSM, pThis->u32LinkSpeed);
}


/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC, Pass 0 only.}
 */
static DECLCALLBACK(int) pcnetR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    RT_NOREF(uPass);
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    pcnetR3SaveConfig(pDevIns->pHlpR3, pThis, pSSM);
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEPREP,
 *      Serializes the receive thread, it may be working inside the critsect.}
 */
static DECLCALLBACK(int) pcnetR3SavePrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRCReturn(rc, rc);
    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) pcnetR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PPCNETSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    pHlp->pfnSSMPutBool(pSSM, pThis->fLinkUp);
    pHlp->pfnSSMPutU32(pSSM, pThis->u32RAP);
    pHlp->pfnSSMPutS32(pSSM, pThis->iISR);
    pHlp->pfnSSMPutU32(pSSM, pThis->u32Lnkst);
    pHlp->pfnSSMPutBool(pSSM, false/* was ffPrivIfEnabled */);     /* >= If version 0.9 */
    pHlp->pfnSSMPutBool(pSSM, pThis->fSignalRxMiss);               /* >= If version 0.10 */
    pHlp->pfnSSMPutGCPhys32(pSSM, pThis->GCRDRA);
    pHlp->pfnSSMPutGCPhys32(pSSM, pThis->GCTDRA);
    pHlp->pfnSSMPutMem(pSSM, pThis->aPROM, sizeof(pThis->aPROM));
    pHlp->pfnSSMPutMem(pSSM, pThis->aCSR, sizeof(pThis->aCSR));
    pHlp->pfnSSMPutMem(pSSM, pThis->aBCR, sizeof(pThis->aBCR));
    pHlp->pfnSSMPutMem(pSSM, pThis->aMII, sizeof(pThis->aMII));
    pHlp->pfnSSMPutU16(pSSM, pThis->u16CSR0LastSeenByGuest);
    pHlp->pfnSSMPutU64(pSSM, pThis->u64LastPoll);
    pcnetR3SaveConfig(pHlp, pThis, pSSM);

    int rc = VINF_SUCCESS;
    rc = PDMDevHlpTimerSave(pDevIns, pThis->hTimerPoll, pSSM);
    if (RT_FAILURE(rc))
        return rc;
    if (pThis->uDevType == DEV_AM79C973)
        rc = PDMDevHlpTimerSave(pDevIns, pThis->hTimerSoftInt, pSSM);
    return rc;
}


/**
 * @callback_method_impl{FNSSMDEVLOADPREP},
 *      Serializes the receive thread, it may be working inside the critsect.}
 */
static DECLCALLBACK(int) pcnetR3LoadPrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PPCNETSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRCReturn(rc, rc);

    uint32_t uVer = pHlp->pfnSSMHandleVersion(pSSM);
    if (    uVer  < VBOX_FULL_VERSION_MAKE(4, 3,  6)
        || (   uVer >= VBOX_FULL_VERSION_MAKE(4, 3, 51)
            && uVer <  VBOX_FULL_VERSION_MAKE(4, 3, 53)))
    {
        /* older saved states contain the shared memory region which was never used for ages. */
        void *pvSharedMMIOR3;
        rc = PDMDevHlpMmio2Create(pDevIns, pDevIns->apPciDevs[0], 2 << 16, _512K, 0, "PCnetSh",
                                  &pvSharedMMIOR3, &pThis->hMmio2Shared);
        if (RT_FAILURE(rc))
            rc = PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                     N_("Failed to allocate the dummy shmem region for the PCnet device"));
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) pcnetR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PPCNETSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    if (   SSM_VERSION_MAJOR_CHANGED(uVersion, PCNET_SAVEDSTATE_VERSION)
        || SSM_VERSION_MINOR(uVersion) < 7)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    if (uPass == SSM_PASS_FINAL)
    {
        /* restore data */
        pHlp->pfnSSMGetBool(pSSM, &pThis->fLinkUp);
        int rc = pHlp->pfnSSMGetU32(pSSM, &pThis->u32RAP);
        AssertRCReturn(rc, rc);
        AssertLogRelMsgReturn(pThis->u32RAP < RT_ELEMENTS(pThis->aCSR), ("%#x\n", pThis->u32RAP), VERR_SSM_LOAD_CONFIG_MISMATCH);
        pHlp->pfnSSMGetS32(pSSM, &pThis->iISR);
        pHlp->pfnSSMGetU32(pSSM, &pThis->u32Lnkst);
        if (   SSM_VERSION_MAJOR(uVersion) >  0
            || SSM_VERSION_MINOR(uVersion) >= 9)
        {
            bool fPrivIfEnabled = false;
            pHlp->pfnSSMGetBool(pSSM, &fPrivIfEnabled);
            if (fPrivIfEnabled)
            {
                /* no longer implemented */
                LogRel(("PCnet#%d: Cannot enable private interface!\n", PCNET_INST_NR));
                return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
            }
        }
        if (   SSM_VERSION_MAJOR(uVersion) >  0
            || SSM_VERSION_MINOR(uVersion) >= 10)
            pHlp->pfnSSMGetBool(pSSM, &pThis->fSignalRxMiss);
        pHlp->pfnSSMGetGCPhys32(pSSM, &pThis->GCRDRA);
        pHlp->pfnSSMGetGCPhys32(pSSM, &pThis->GCTDRA);
        pHlp->pfnSSMGetMem(pSSM, &pThis->aPROM, sizeof(pThis->aPROM));
        pHlp->pfnSSMGetMem(pSSM, &pThis->aCSR, sizeof(pThis->aCSR));
        pHlp->pfnSSMGetMem(pSSM, &pThis->aBCR, sizeof(pThis->aBCR));
        pHlp->pfnSSMGetMem(pSSM, &pThis->aMII, sizeof(pThis->aMII));
        pHlp->pfnSSMGetU16(pSSM, &pThis->u16CSR0LastSeenByGuest);
        pHlp->pfnSSMGetU64(pSSM, &pThis->u64LastPoll);
    }

    /* check config */
    RTMAC       Mac;
    int rc = pHlp->pfnSSMGetMem(pSSM, &Mac, sizeof(Mac));
    AssertRCReturn(rc, rc);
    if (    memcmp(&Mac, &pThis->MacConfigured, sizeof(Mac))
        && (uPass == 0 || !PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns)) )
        LogRel(("PCnet#%u: The mac address differs: config=%RTmac saved=%RTmac\n", PCNET_INST_NR, &pThis->MacConfigured, &Mac));

    uint8_t     uDevType;
    rc = pHlp->pfnSSMGetU8(pSSM, &uDevType);
    AssertRCReturn(rc, rc);
    if (pThis->uDevType != uDevType)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("The uDevType setting differs: config=%u saved=%u"), pThis->uDevType, uDevType);

    uint32_t    u32LinkSpeed;
    rc = pHlp->pfnSSMGetU32(pSSM, &u32LinkSpeed);
    AssertRCReturn(rc, rc);
    if (    pThis->u32LinkSpeed != u32LinkSpeed
        && (uPass == 0 || !PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns)) )
        LogRel(("PCnet#%u: The mac link speed differs: config=%u saved=%u\n", PCNET_INST_NR, pThis->u32LinkSpeed, u32LinkSpeed));

    if (uPass == SSM_PASS_FINAL)
    {
        /* restore timers and stuff */
        PDMDevHlpTimerLoad(pDevIns, pThis->hTimerPoll, pSSM);
        if (pThis->uDevType == DEV_AM79C973)
        {
            if (   SSM_VERSION_MAJOR(uVersion) >  0
                || SSM_VERSION_MINOR(uVersion) >= 8)
                PDMDevHlpTimerLoad(pDevIns, pThis->hTimerSoftInt, pSSM);
        }

        pThis->iLog2DescSize = BCR_SWSTYLE(pThis)
                             ? 4
                             : 3;
        pThis->GCUpperPhys   = BCR_SSIZE32(pThis)
                             ? 0
                             : (0xff00 & (uint32_t)pThis->aCSR[2]) << 16;

        /* update promiscuous mode. */
        if (pThisCC->pDrv)
            pThisCC->pDrv->pfnSetPromiscuousMode(pThisCC->pDrv, CSR_PROM(pThis));

        /* Indicate link down to the guest OS that all network connections have
           been lost, unless we've been teleported here. */
        if (!PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns))
            pcnetR3TempLinkDown(pDevIns, pThis);
    }

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADDONE}
 */
static DECLCALLBACK(int) pcnetR3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    int rc = VINF_SUCCESS;
    if (pThis->hMmio2Shared != NIL_PGMMMIO2HANDLE)
    {
        /* drop this dummy region */
        rc = pDevIns->pHlpR3->pfnMmio2Destroy(pDevIns, pThis->hMmio2Shared);
        AssertLogRelRC(rc);
        pThis->hMmio2Shared = NIL_PGMMMIO2HANDLE;
    }
    return rc;
}

/* -=-=-=-=-=- PCNETSTATE::INetworkDown -=-=-=-=-=- */

/**
 * Check if the device/driver can receive data now.
 *
 * Worker for pcnetR3NetworkDown_WaitReceiveAvail().  This must be called before
 * the pfnRecieve() method is called.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The PCnet shared instance data.
 */
static int pcnetR3CanReceive(PPDMDEVINS pDevIns, PPCNETSTATE pThis)
{
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rc);

    rc = VERR_NET_NO_BUFFER_SPACE;

    if (RT_LIKELY(!CSR_DRX(pThis) && !CSR_STOP(pThis) && !CSR_SPND(pThis)))
    {
        if (HOST_IS_OWNER(CSR_CRST(pThis)) && pThis->GCRDRA)
            pcnetRdtePoll(pDevIns, pThis);

        if (RT_UNLIKELY(HOST_IS_OWNER(CSR_CRST(pThis))))
        {
            /** @todo Notify the guest _now_. Will potentially increase the interrupt load */
            if (pThis->fSignalRxMiss)
                pThis->aCSR[0] |= 0x1000; /* Set MISS flag */
        }
        else
            rc = VINF_SUCCESS;
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnWaitReceiveAvail}
 */
static DECLCALLBACK(int) pcnetR3NetworkDown_WaitReceiveAvail(PPDMINETWORKDOWN pInterface, RTMSINTERVAL cMillies)
{
    PPCNETSTATECC pThisCC = RT_FROM_MEMBER(pInterface, PCNETSTATECC, INetworkDown);
    PPDMDEVINS    pDevIns = pThisCC->pDevIns;
    PPCNETSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);

    int rc = pcnetR3CanReceive(pDevIns, pThis);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;
    if (RT_UNLIKELY(cMillies == 0))
        return VERR_NET_NO_BUFFER_SPACE;

    rc = VERR_INTERRUPTED;
    ASMAtomicXchgBool(&pThis->fMaybeOutOfSpace, true);
    STAM_PROFILE_START(&pThis->StatRxOverflow, a);
    VMSTATE enmVMState;
    while (RT_LIKELY(   (enmVMState = PDMDevHlpVMState(pDevIns)) == VMSTATE_RUNNING
                     || enmVMState == VMSTATE_RUNNING_LS))
    {
        int rc2 = pcnetR3CanReceive(pDevIns, pThis);
        if (RT_SUCCESS(rc2))
        {
            rc = VINF_SUCCESS;
            break;
        }
        LogFlow(("pcnetR3NetworkDown_WaitReceiveAvail: waiting cMillies=%u...\n", cMillies));
        /* Start the poll timer once which will remain active as long fMaybeOutOfSpace
         * is true -- even if (transmit) polling is disabled (CSR_DPOLL). */
        rc2 = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rc2);
        pcnetPollTimerStart(pDevIns, pThis);
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->hEventOutOfRxSpace, cMillies);
    }
    STAM_PROFILE_STOP(&pThis->StatRxOverflow, a);
    ASMAtomicXchgBool(&pThis->fMaybeOutOfSpace, false);

    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnReceive}
 */
static DECLCALLBACK(int) pcnetR3NetworkDown_Receive(PPDMINETWORKDOWN pInterface, const void *pvBuf, size_t cb)
{
    PPCNETSTATECC pThisCC = RT_FROM_MEMBER(pInterface, PCNETSTATECC, INetworkDown);
    PPDMDEVINS    pDevIns = pThisCC->pDevIns;
    PPCNETSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);

    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rc);

    /*
     * Check for the max ethernet frame size, taking the IEEE 802.1Q (VLAN) tag into
     * account. Note that the CRC Checksum is optional.
     * Ethernet frames consist of a 14-byte header [+ 4-byte vlan tag] + a 1500-byte body [+ 4-byte CRC].
     */
    if (RT_LIKELY(   cb <= 1518
                  || (   cb <= 1522
                      && ((PCRTNETETHERHDR)pvBuf)->EtherType == RT_H2BE_U16_C(RTNET_ETHERTYPE_VLAN))))
    {
        bool fAddFCS =   cb <= 1514
                      || (   cb <= 1518
                          && ((PCRTNETETHERHDR)pvBuf)->EtherType == RT_H2BE_U16_C(RTNET_ETHERTYPE_VLAN));
        if (cb > 70) /* unqualified guess */
            pThis->Led.Asserted.s.fReading = pThis->Led.Actual.s.fReading = 1;
        pcnetReceiveNoSync(pDevIns, pThis, pThisCC, (const uint8_t *)pvBuf, cb, fAddFCS, false);
        pThis->Led.Actual.s.fReading = 0;
    }
#ifdef LOG_ENABLED
    else
    {
        static bool s_fFirstBigFrameLoss = true;
        unsigned cbMaxFrame = ((PCRTNETETHERHDR)pvBuf)->EtherType == RT_H2BE_U16_C(RTNET_ETHERTYPE_VLAN)
                            ? 1522 : 1518;
        if (s_fFirstBigFrameLoss)
        {
            s_fFirstBigFrameLoss = false;
            Log(("PCnet#%d: Received giant frame %zu, max %u. (Further giants will be reported at level5.)\n",
                 PCNET_INST_NR, cb, cbMaxFrame));
        }
        else
            Log5(("PCnet#%d: Received giant frame %zu bytes, max %u.\n",
                  PCNET_INST_NR, cb, cbMaxFrame));
    }
#endif /* LOG_ENABLED */

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnXmitPending}
 */
static DECLCALLBACK(void) pcnetR3NetworkDown_XmitPending(PPDMINETWORKDOWN pInterface)
{
    PPCNETSTATECC pThisCC = RT_FROM_MEMBER(pInterface, PCNETSTATECC, INetworkDown);
    PPDMDEVINS    pDevIns = pThisCC->pDevIns;
    PPCNETSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    pcnetXmitPending(pDevIns, pThis, pThisCC, true /*fOnWorkerThread*/);
}


/* -=-=-=-=-=- PCNETSTATE::INetworkConfig -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetMac}
 */
static DECLCALLBACK(int) pcnetR3NetworkConfig_GetMac(PPDMINETWORKCONFIG pInterface, PRTMAC pMac)
{
    PPCNETSTATECC pThisCC = RT_FROM_MEMBER(pInterface, PCNETSTATECC, INetworkConfig);
    PPDMDEVINS    pDevIns = pThisCC->pDevIns;
    PPCNETSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    memcpy(pMac, pThis->aPROM, sizeof(*pMac));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetLinkState}
 */
static DECLCALLBACK(PDMNETWORKLINKSTATE) pcnetR3NetworkConfig_GetLinkState(PPDMINETWORKCONFIG pInterface)
{
    PPCNETSTATECC pThisCC = RT_FROM_MEMBER(pInterface, PCNETSTATECC, INetworkConfig);
    PPDMDEVINS    pDevIns = pThisCC->pDevIns;
    PPCNETSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    if (pThis->fLinkUp && !pThis->fLinkTempDown)
        return PDMNETWORKLINKSTATE_UP;
    if (!pThis->fLinkUp)
        return PDMNETWORKLINKSTATE_DOWN;
    if (pThis->fLinkTempDown)
        return PDMNETWORKLINKSTATE_DOWN_RESUME;
    AssertMsgFailed(("Invalid link state!\n"));
    return PDMNETWORKLINKSTATE_INVALID;
}


/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnSetLinkState}
 */
static DECLCALLBACK(int) pcnetR3NetworkConfig_SetLinkState(PPDMINETWORKCONFIG pInterface, PDMNETWORKLINKSTATE enmState)
{
    PPCNETSTATECC pThisCC = RT_FROM_MEMBER(pInterface, PCNETSTATECC, INetworkConfig);
    PPDMDEVINS    pDevIns = pThisCC->pDevIns;
    PPCNETSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    bool fLinkUp;

    AssertMsgReturn(enmState > PDMNETWORKLINKSTATE_INVALID && enmState <= PDMNETWORKLINKSTATE_DOWN_RESUME,
                    ("Invalid link state: enmState=%d\n", enmState), VERR_INVALID_PARAMETER);

    if (enmState == PDMNETWORKLINKSTATE_DOWN_RESUME)
    {
        pcnetR3TempLinkDown(pDevIns, pThis);
        /*
         * Note that we do not notify the driver about the link state change because
         * the change is only temporary and can be disregarded from the driver's
         * point of view (see @bugref{7057}).
         */
        return VINF_SUCCESS;
    }
    /* has the state changed? */
    fLinkUp = enmState == PDMNETWORKLINKSTATE_UP;
    if (pThis->fLinkUp != fLinkUp)
    {
        pThis->fLinkUp = fLinkUp;
        if (fLinkUp)
        {
            /* Connect with a configured delay. */
            pThis->fLinkTempDown = true;
            pThis->cLinkDownReported = 0;
            pThis->aCSR[0] |= RT_BIT(15) | RT_BIT(13); /* ERR | CERR (this is probably wrong) */
            pThis->Led.Asserted.s.fError = pThis->Led.Actual.s.fError = 1;
            int rc = PDMDevHlpTimerSetMillies(pDevIns, pThis->hTimerRestore, pThis->cMsLinkUpDelay);
            AssertRC(rc);
        }
        else
        {
            /* disconnect */
            pThis->cLinkDownReported = 0;
            pThis->aCSR[0] |= RT_BIT(15) | RT_BIT(13); /* ERR | CERR (this is probably wrong) */
            pThis->Led.Asserted.s.fError = pThis->Led.Actual.s.fError = 1;
        }
        Assert(!PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
        if (pThisCC->pDrv)
            pThisCC->pDrv->pfnNotifyLinkChanged(pThisCC->pDrv, enmState);
    }
    return VINF_SUCCESS;
}


/* -=-=-=-=-=- PCNETSTATE::ILeds (LUN#0) -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMILEDPORTS,pfnQueryStatusLed}
 */
static DECLCALLBACK(int) pcnetQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PPCNETSTATECC pThisCC = RT_FROM_MEMBER(pInterface, PCNETSTATECC, ILeds);
    PPDMDEVINS    pDevIns = pThisCC->pDevIns;
    PPCNETSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    if (iLUN == 0)
    {
        *ppLed = &pThis->Led;
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/* -=-=-=-=-=- PCNETSTATE::IBase (LUN#0) -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) pcnetQueryInterface(struct PDMIBASE *pInterface, const char *pszIID)
{
    PPCNETSTATECC pThisCC = RT_FROM_MEMBER(pInterface, PCNETSTATECC, IBase);
    Assert(&pThisCC->IBase == pInterface);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKDOWN, &pThisCC->INetworkDown);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKCONFIG, &pThisCC->INetworkConfig);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pThisCC->ILeds);
    return NULL;
}


/* -=-=-=-=-=- PDMDEVREG -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMDEVREG,pfnPowerOff}
 */
static DECLCALLBACK(void) pcnetR3PowerOff(PPDMDEVINS pDevIns)
{
    /* Poke thread waiting for buffer space. */
    pcnetWakeupReceive(pDevIns);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDetach}
 *
 * One port on the network card has been disconnected from the network.
 */
static DECLCALLBACK(void) pcnetR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PPCNETSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    Log(("#%d pcnetR3Detach:\n", PCNET_INST_NR));
    RT_NOREF(fFlags);

    AssertLogRelReturnVoid(iLUN == 0);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    /** @todo r=pritesh still need to check if i missed
     * to clean something in this function
     */

    /*
     * Zero some important members.
     */
    pThis->fDriverAttached = false;
    pThisCC->pDrvBase = NULL;
    pThisCC->pDrv = NULL;
    /// @todo figure this out
    //pThis->pDrvR0 = NIL_RTR0PTR;
    //pThis->pDrvRC = NIL_RTRCPTR;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnAttach}
 * One port on the network card has been connected to a network.
 */
static DECLCALLBACK(int) pcnetR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PPCNETSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    LogFlow(("#%d pcnetR3Attach:\n", PCNET_INST_NR));
    RT_NOREF(fFlags);

    AssertLogRelReturn(iLUN == 0, VERR_PDM_NO_SUCH_LUN);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    /*
     * Attach the driver.
     */
    int rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->IBase, &pThisCC->pDrvBase, "Network Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMINETWORKUP);
        AssertMsgStmt(pThisCC->pDrv, ("Failed to obtain the PDMINETWORKUP interface!\n"),
                      rc = VERR_PDM_MISSING_INTERFACE_BELOW);
        pThis->fDriverAttached = true;
        /// @todo figoure out this
        //pThis->pDrvR0 = PDMIBASER0_QUERY_INTERFACE(PDMIBASE_QUERY_INTERFACE(pThis->pDrvBase, PDMIBASER0), PDMINETWORKUP);
        //pThis->pDrvRC = PDMIBASERC_QUERY_INTERFACE(PDMIBASE_QUERY_INTERFACE(pThis->pDrvBase, PDMIBASERC), PDMINETWORKUP);
    }
    else if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
             || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        /* This should never happen because this function is not called
         * if there is no driver to attach! */
        Log(("#%d No attached driver!\n", PCNET_INST_NR));
    }

    /*
     * Temporary set the link down if it was up so that the guest
     * will know that we have change the configuration of the
     * network card
     */
    if (RT_SUCCESS(rc))
        pcnetR3TempLinkDown(pDevIns, pThis);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;

}


/**
 * @interface_method_impl{PDMDEVREG,pfnSuspend}
 */
static DECLCALLBACK(void) pcnetR3Suspend(PPDMDEVINS pDevIns)
{
    /* Poke thread waiting for buffer space. */
    pcnetWakeupReceive(pDevIns);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) pcnetR3Reset(PPDMDEVINS pDevIns)
{
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    if (pThis->fLinkTempDown)
    {
        pThis->cLinkDownReported = 0x10000;
        PDMDevHlpTimerStop(pDevIns, pThis->hTimerRestore);
        pcnetR3TimerRestore(pDevIns, pThis->hTimerRestore, pThis);
    }

    /** @todo How to flush the queues? */
    pcnetR3HardReset(pDevIns, pThis);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) pcnetR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    PPCNETSTATERC pThisRC = PDMINS_2_DATA_RC(pDevIns, PPCNETSTATERC);
    pThisRC->pDrv += offDelta;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) pcnetR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);

    if (pThis->hEventOutOfRxSpace == NIL_SUPSEMEVENT)
    {
        PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEventOutOfRxSpace);
        PDMDevHlpSUPSemEventClose(pDevIns, pThis->hEventOutOfRxSpace);
        pThis->hEventOutOfRxSpace = NIL_SUPSEMEVENT;
    }

    if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->CritSect))
        PDMDevHlpCritSectDelete(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) pcnetR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PPCNETSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);
    PPCNETSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPCNETSTATECC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    PPDMIBASE       pBase;
    char            szTmp[128];
    int             rc;

    Assert(RT_ELEMENTS(pThis->aBCR) == BCR_MAX_RAP);
    Assert(RT_ELEMENTS(pThis->aMII) == MII_MAX_REG);
    Assert(sizeof(pThis->abLoopBuf) == RT_ALIGN_Z(sizeof(pThis->abLoopBuf), 16));

    /*
     * Init what's required to make the destructor safe.
     */
    pThis->iInstance            = iInstance;
    pThis->hEventOutOfRxSpace   = NIL_SUPSEMEVENT;
    pThis->hIoPortsPci          = NIL_IOMIOPORTHANDLE;
    pThis->hIoPortsPciAProm     = NIL_IOMIOPORTHANDLE;
    pThis->hIoPortsIsa          = NIL_IOMIOPORTHANDLE;
    pThis->hIoPortsIsaAProm     = NIL_IOMIOPORTHANDLE;
    pThis->hMmio2Shared         = NIL_PGMMMIO2HANDLE;
    pThisCC->pDevIns            = pDevIns;

    /*
     * Validate configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,
                                  "MAC|CableConnected|Am79C973|ChipType|Port|IRQ|LineSpeed|PrivIfEnabled|LinkUpDelay|StatNo",
                                  "");
    /*
     * Read the configuration.
     */
    rc = pHlp->pfnCFGMQueryBytes(pCfg, "MAC", &pThis->MacConfigured, sizeof(pThis->MacConfigured));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"MAC\" value"));
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CableConnected", &pThis->fLinkUp, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"CableConnected\" value"));

    /*
     * Determine the model.
     */
    char szChipType[16];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "ChipType", &szChipType[0], sizeof(szChipType), "Am79C970A");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"ChipType\" as string failed"));

    if (!strcmp(szChipType, "Am79C970A"))
        pThis->uDevType = DEV_AM79C970A;    /* 10 Mbps PCnet-PCI II. */
    else if (!strcmp(szChipType, "Am79C973"))
        pThis->uDevType = DEV_AM79C973;     /* 10/100 Mbps PCnet-FAST III. */
    else if (!strcmp(szChipType, "Am79C960"))
        pThis->uDevType = DEV_AM79C960;     /* 10 Mbps PCnet-ISA, NE2100/Am2100 compatible. */
    else if (!strcmp(szChipType, "Am79C960_EB"))
    {
        pThis->uDevType = DEV_AM79C960_EB;  /* 10 Mbps PCnet-ISA, Racal InterLink NI6510 EtherBlaster compatible. */
        /* NI6510 drivers (at least Racal's and Linux) require the OUI to be InterLan's (Racal-Datacom).
         * Refuse loading if OUI doesn't match, because otherwise drivers won't load in the guest. */
        if (memcmp(&pThis->MacConfigured, "\x02\x07\x01", 3))
            return PDMDevHlpVMSetError(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES, RT_SRC_POS,
                                       N_("Configuration error: MAC address OUI for EtherBlaster must be 02 07 01"));
    }
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES, RT_SRC_POS,
                                   N_("Configuration error: The \"ChipType\" value \"%s\" is unsupported"),
                                   szChipType);


    /*
     * Process the old model configuration. If present, it must take precedence for saved state compatibility.
     */
    bool fAm79C973;
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Am79C973", &fAm79C973, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"Am79C973\" value"));
    if (fAm79C973)
        pThis->uDevType = DEV_AM79C973;

    /*
     * Process ISA configuration options. The defaults are chosen to be NE2100/Am2100 compatible.
     */
    rc = pHlp->pfnCFGMQueryPortDef(pCfg, "Port", &pThis->IOPortBase, 0x300);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"Port\" value"));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "IRQ", &pThis->uIsaIrq, 3);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"IRQ\" value"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "LineSpeed", &pThis->u32LinkSpeed, 1000000); /* 1GBit/s (in kbps units)*/
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"LineSpeed\" value"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "LinkUpDelay", (uint32_t*)&pThis->cMsLinkUpDelay, 5000); /* ms */
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the value of 'LinkUpDelay'"));
    Assert(pThis->cMsLinkUpDelay <= 300000); /* less than 5 minutes */
    if (pThis->cMsLinkUpDelay > 5000 || pThis->cMsLinkUpDelay < 100)
        LogRel(("PCnet#%d WARNING! Link up delay is set to %u seconds!\n", iInstance, pThis->cMsLinkUpDelay / 1000));
    Log(("#%d Link up delay is set to %u seconds\n", iInstance, pThis->cMsLinkUpDelay / 1000));

    uint32_t uStatNo = iInstance;
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "StatNo", &uStatNo, iInstance);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"StatNo\" value"));

    /*
     * Initialize data (most of it anyway).
     */
    pThis->Led.u32Magic                         = PDMLED_MAGIC;
    /* IBase */
    pThisCC->IBase.pfnQueryInterface            = pcnetQueryInterface;
    /* INeworkPort */
    pThisCC->INetworkDown.pfnWaitReceiveAvail   = pcnetR3NetworkDown_WaitReceiveAvail;
    pThisCC->INetworkDown.pfnReceive            = pcnetR3NetworkDown_Receive;
    pThisCC->INetworkDown.pfnXmitPending        = pcnetR3NetworkDown_XmitPending;
    /* INetworkConfig */
    pThisCC->INetworkConfig.pfnGetMac           = pcnetR3NetworkConfig_GetMac;
    pThisCC->INetworkConfig.pfnGetLinkState     = pcnetR3NetworkConfig_GetLinkState;
    pThisCC->INetworkConfig.pfnSetLinkState     = pcnetR3NetworkConfig_SetLinkState;
    /* ILeds */
    pThisCC->ILeds.pfnQueryStatusLed            = pcnetQueryStatusLed;

    /* PCI Device */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,       0x1022);
    PDMPciDevSetDeviceId(pPciDev,       0x2000);
    PDMPciDevSetByte(pPciDev, 0x04,     0x07); /* command */
    PDMPciDevSetByte(pPciDev, 0x05,     0x00);
    PDMPciDevSetByte(pPciDev, 0x06,     0x80); /* status */
    PDMPciDevSetByte(pPciDev, 0x07,     0x02);
    PDMPciDevSetByte(pPciDev, 0x08,     pThis->uDevType == DEV_AM79C973 ? 0x40 : 0x16); /* revision */
    PDMPciDevSetByte(pPciDev, 0x09,     0x00);
    PDMPciDevSetByte(pPciDev, 0x0a,     0x00); /* ethernet network controller */
    PDMPciDevSetByte(pPciDev, 0x0b,     0x02);
    PDMPciDevSetByte(pPciDev, 0x0e,     0x00); /* header_type */
    PDMPciDevSetByte(pPciDev, 0x10,     0x01); /* IO Base */
    PDMPciDevSetByte(pPciDev, 0x11,     0x00);
    PDMPciDevSetByte(pPciDev, 0x12,     0x00);
    PDMPciDevSetByte(pPciDev, 0x13,     0x00);
    PDMPciDevSetByte(pPciDev, 0x14,     0x00); /* MMIO Base */
    PDMPciDevSetByte(pPciDev, 0x15,     0x00);
    PDMPciDevSetByte(pPciDev, 0x16,     0x00);
    PDMPciDevSetByte(pPciDev, 0x17,     0x00);

    /* subsystem and subvendor IDs */
    PDMPciDevSetByte(pPciDev, 0x2c,     0x22); /* subsystem vendor id */
    PDMPciDevSetByte(pPciDev, 0x2d,     0x10);
    PDMPciDevSetByte(pPciDev, 0x2e,     0x00); /* subsystem id */
    PDMPciDevSetByte(pPciDev, 0x2f,     0x20);
    PDMPciDevSetByte(pPciDev, 0x3d,     1);    /* interrupt pin 0 */
    PDMPciDevSetByte(pPciDev, 0x3e,     0x06);
    PDMPciDevSetByte(pPciDev, 0x3f,     0xff);

    /*
     * We use our own critical section (historical reasons).
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "PCnet#%u", iInstance);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, &pThis->CritSect);
    AssertRCReturn(rc, rc);

    /*
     * Register the PCI device, its I/O regions, the timer and the saved state item.
     */
    Assert(PCNET_IS_PCI(pThis) != PCNET_IS_ISA(pThis)); /* IOPortBase is shared, so it's either one or the other! */

    if (PCNET_IS_PCI(pThis))
    {
        rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
        AssertRCReturn(rc, rc);

        /* Region #0: I/O ports - two handlers: */
        rc = PDMDevHlpIoPortCreate(pDevIns, 0x10 /*cPorts*/, pPciDev, 0 /*iPciRegion*/,
                                   pcnetIoPortAPromWrite, pcnetIOPortAPromRead, NULL /*pvUser*/,
                                   "PCnet APROM",  NULL /*paExtDescs*/, &pThis->hIoPortsPciAProm);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortCreate(pDevIns, 0x10 /*cPorts*/, pPciDev, 0 /*iPciRegion*/,
                                   pcnetIoPortWrite, pcnetIoPortRead, NULL /*pvUser*/,
                                   "PCnet",        NULL /*paExtDescs*/, &pThis->hIoPortsPci);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpPCIIORegionRegisterIoCustom(pDevIns, 0, PCNET_IOPORT_SIZE, pcnetR3PciMapUnmapIoPorts);
        AssertRCReturn(rc, rc);

        /* Region #1: MMIO */
        rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 1 /*iPciRegion*/, PCNET_PNPMMIO_SIZE, PCI_ADDRESS_SPACE_MEM,
                                            pcnetR3MmioWrite, pcnetR3MmioRead, NULL /*pvUser*/,
                                            IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                                            "PCnet", &pThis->hMmioPci);
        AssertRCReturn(rc, rc);
    }

    /*
     * Register ISA I/O ranges for PCnet-ISA.
     */
    if (PCNET_IS_ISA(pThis))
    {
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOPortBase, 0x10 /*cPorts*/, pcnetIoPortAPromWrite, pcnetIOPortAPromRead,
                                         "PCnet APROM", NULL /*paExtDesc*/, &pThis->hIoPortsIsaAProm);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOPortBase + 0x10, 0x10 /*cPorts*/, pcnetIoPortWrite, pcnetIoPortRead,
                                         "PCnet",       NULL /*paExtDesc*/, &pThis->hIoPortsIsa);
        AssertRCReturn(rc, rc);
    }


    /* Transmit descriptor polling timer */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, pcnetR3Timer, NULL, TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0,
                              "PCnet Poll", &pThis->hTimerPoll);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpTimerSetCritSect(pDevIns, pThis->hTimerPoll, &pThis->CritSect);
    AssertRCReturn(rc, rc);

    if (pThis->uDevType == DEV_AM79C973)
    {
        /* Software Interrupt timer */
        rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, pcnetR3TimerSoftInt, NULL,
                                  TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "PCnet SoftInt", &pThis->hTimerSoftInt);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpTimerSetCritSect(pDevIns, pThis->hTimerSoftInt, &pThis->CritSect);
        AssertRCReturn(rc, rc);
    }
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, pcnetR3TimerRestore, pThis,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_NO_RING0, "PCnet Restore", &pThis->hTimerRestore);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpSSMRegisterEx(pDevIns, PCNET_SAVEDSTATE_VERSION, sizeof(*pThis), NULL,
                                NULL,            pcnetR3LiveExec, NULL,
                                pcnetR3SavePrep, pcnetR3SaveExec, NULL,
                                pcnetR3LoadPrep, pcnetR3LoadExec, pcnetR3LoadDone);
    AssertRCReturn(rc, rc);

    /*
     * Create the transmit queue.
     */
    rc = PDMDevHlpTaskCreate(pDevIns, PDMTASK_F_RZ, "PCnet-Xmit", pcnetR3XmitTaskCallback, NULL /*pvUser*/, &pThis->hXmitTask);
    AssertRCReturn(rc, rc);

    /*
     * Create the RX notifier semaphore.
     */
    rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pThis->hEventOutOfRxSpace);
    AssertRCReturn(rc, rc);

    /*
     * Attach status driver (optional).
     */
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pBase, "Status Port");
    if (RT_SUCCESS(rc))
        pThisCC->pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
    else
        AssertMsgReturn(   rc == VERR_PDM_NO_ATTACHED_DRIVER
                        || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME,
                        ("Failed to attach to status driver. rc=%Rrc\n", rc),
                        rc);

    /*
     * Attach driver.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->IBase, &pThisCC->pDrvBase, "Network Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMINETWORKUP);
        AssertMsgReturn(pThisCC->pDrv, ("Failed to obtain the PDMINETWORKUP interface!\n"),
                        VERR_PDM_MISSING_INTERFACE_BELOW);
        pThis->fDriverAttached = true;
        /// @todo figure out this!
        //pThis->pDrvR0 = PDMIBASER0_QUERY_INTERFACE(PDMIBASE_QUERY_INTERFACE(pThis->pDrvBase, PDMIBASER0), PDMINETWORKUP);
        //pThis->pDrvRC = PDMIBASERC_QUERY_INTERFACE(PDMIBASE_QUERY_INTERFACE(pThis->pDrvBase, PDMIBASERC), PDMINETWORKUP);
    }
    else if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
             || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        /* No error! */
        Log(("No attached driver!\n"));
    }
    else
        return rc;

    /*
     * Reset the device state. (Do after attaching.)
     */
    pcnetR3HardReset(pDevIns, pThis);

    /*
     * Register the info item.
     */
    RTStrPrintf(szTmp, sizeof(szTmp), "pcnet%d", pDevIns->iInstance);
    PDMDevHlpDBGFInfoRegister(pDevIns, szTmp, "PCNET info.", pcnetR3Info);

    /*
     * Register statistics.
     * The /Public/ bits are official and used by session info in the GUI.
     */
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatReceiveBytes,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           "Amount of data received",     "/Public/NetAdapter/%u/BytesReceived", uStatNo);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatTransmitBytes,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           "Amount of data transmitted",  "/Public/NetAdapter/%u/BytesTransmitted", uStatNo);
    PDMDevHlpSTAMRegisterF(pDevIns, &pDevIns->iInstance,           STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                           "Device instance number",      "/Public/NetAdapter/%u/%s", uStatNo, pDevIns->pReg->szName);

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceiveBytes,       STAMTYPE_COUNTER, "ReceiveBytes",       STAMUNIT_BYTES,               "Amount of data received");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitBytes,      STAMTYPE_COUNTER, "TransmitBytes",      STAMUNIT_BYTES,               "Amount of data transmitted");

#ifdef VBOX_WITH_STATISTICS
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOReadRZ,         STAMTYPE_PROFILE, "MMIO/ReadRZ",        STAMUNIT_TICKS_PER_CALL,      "Profiling MMIO reads in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOReadR3,         STAMTYPE_PROFILE, "MMIO/ReadR3",        STAMUNIT_TICKS_PER_CALL,      "Profiling MMIO reads in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOWriteRZ,        STAMTYPE_PROFILE, "MMIO/WriteRZ",       STAMUNIT_TICKS_PER_CALL,      "Profiling MMIO writes in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOWriteR3,        STAMTYPE_PROFILE, "MMIO/WriteR3",       STAMUNIT_TICKS_PER_CALL,      "Profiling MMIO writes in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAPROMRead,          STAMTYPE_PROFILE, "IO/APROMRead",       STAMUNIT_TICKS_PER_CALL,      "Profiling APROM reads");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAPROMWrite,         STAMTYPE_PROFILE, "IO/APROMWrite",      STAMUNIT_TICKS_PER_CALL,      "Profiling APROM writes");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIOReadRZ,           STAMTYPE_PROFILE, "IO/ReadRZ",          STAMUNIT_TICKS_PER_CALL,      "Profiling IO reads in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIOReadR3,           STAMTYPE_PROFILE, "IO/ReadR3",          STAMUNIT_TICKS_PER_CALL,      "Profiling IO reads in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIOWriteRZ,          STAMTYPE_PROFILE, "IO/WriteRZ",         STAMUNIT_TICKS_PER_CALL,      "Profiling IO writes in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIOWriteR3,          STAMTYPE_PROFILE, "IO/WriteR3",         STAMUNIT_TICKS_PER_CALL,      "Profiling IO writes in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTimer,              STAMTYPE_PROFILE, "Timer",              STAMUNIT_TICKS_PER_CALL,      "Profiling Timer");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceive,            STAMTYPE_PROFILE, "Receive",            STAMUNIT_TICKS_PER_CALL,      "Profiling receive");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRxOverflow,         STAMTYPE_PROFILE, "RxOverflow",         STAMUNIT_TICKS_PER_OCCURENCE, "Profiling RX overflows");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRxOverflowWakeup,   STAMTYPE_PROFILE, "RxOverflowWakeup",   STAMUNIT_TICKS_PER_OCCURENCE, "Nr of RX overflow wakeups");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitCase1,      STAMTYPE_COUNTER, "Transmit/Case1",     STAMUNIT_OCCURENCES,          "Single descriptor transmit");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitCase2,      STAMTYPE_COUNTER, "Transmit/Case2",     STAMUNIT_OCCURENCES,          "Multi descriptor transmit");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitRZ,         STAMTYPE_PROFILE, "Transmit/TotalRZ",   STAMUNIT_TICKS_PER_CALL,      "Profiling transmits in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitR3,         STAMTYPE_PROFILE, "Transmit/TotalR3",   STAMUNIT_TICKS_PER_CALL,      "Profiling transmits in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitSendRZ,     STAMTYPE_PROFILE, "Transmit/SendRZ",    STAMUNIT_TICKS_PER_CALL,      "Profiling PCnet send transmit in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitSendR3,     STAMTYPE_PROFILE, "Transmit/SendR3",    STAMUNIT_TICKS_PER_CALL,      "Profiling PCnet send transmit in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTdtePollRZ,         STAMTYPE_PROFILE, "TdtePollRZ",         STAMUNIT_TICKS_PER_CALL,      "Profiling PCnet TdtePoll in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTdtePollR3,         STAMTYPE_PROFILE, "TdtePollR3",         STAMUNIT_TICKS_PER_CALL,      "Profiling PCnet TdtePoll in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdtePollRZ,         STAMTYPE_PROFILE, "RdtePollRZ",         STAMUNIT_TICKS_PER_CALL,      "Profiling PCnet RdtePoll in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdtePollR3,         STAMTYPE_PROFILE, "RdtePollR3",         STAMUNIT_TICKS_PER_CALL,      "Profiling PCnet RdtePoll in R3");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTmdStoreRZ,         STAMTYPE_PROFILE, "TmdStoreRZ",         STAMUNIT_TICKS_PER_CALL,      "Profiling PCnet TmdStore in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTmdStoreR3,         STAMTYPE_PROFILE, "TmdStoreR3",         STAMUNIT_TICKS_PER_CALL,      "Profiling PCnet TmdStore in R3");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatXmitSkipCurrent,    STAMTYPE_COUNTER, "Xmit/Skipped",       STAMUNIT_OCCURENCES,          "");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatInterrupt,          STAMTYPE_PROFILE, "UpdateIRQ",          STAMUNIT_TICKS_PER_CALL,      "Profiling interrupt checks");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatPollTimer,          STAMTYPE_PROFILE, "PollTimer",          STAMUNIT_TICKS_PER_CALL,      "Profiling poll timer");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMIIReads,           STAMTYPE_COUNTER, "MIIReads",           STAMUNIT_OCCURENCES,          "Number of MII reads");
    unsigned i;
    for (i = 0; i < RT_ELEMENTS(pThis->aStatXmitFlush) - 1; i++)
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatXmitFlush[i],  STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "", "XmitFlushIrq/%02u", i + 1);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatXmitFlush[i],      STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                           "", "XmitFlushIrq/%02u-inf", i + 1);

    for (i = 0; i < RT_ELEMENTS(pThis->aStatXmitChainCounts) - 1; i++)
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatXmitChainCounts[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "", "XmitChainCounts/%02u", i + 1);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatXmitChainCounts[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                           "", "XmitChainCounts/%02u-inf", i + 1);
#endif /* VBOX_WITH_STATISTICS */

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) pcnetRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PPCNETSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPCNETSTATE);

    /* Critical section setup: */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, &pThis->CritSect);
    AssertRCReturn(rc, rc);

    /* PCI I/O ports: */
    if (pThis->hIoPortsPciAProm != NIL_IOMIOPORTHANDLE)
    {
        rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsPciAProm, pcnetIoPortAPromWrite, pcnetIOPortAPromRead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsPci, pcnetIoPortWrite, pcnetIoPortRead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
    }
    else
        Assert(pThis->hIoPortsPci == NIL_IOMIOPORTHANDLE);

    /** @todo PCI MMIO   */

    /* ISA I/O ports: */
    if (pThis->hIoPortsIsaAProm != NIL_IOMIOPORTHANDLE)
    {
        rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsIsaAProm, pcnetIoPortAPromWrite, pcnetIOPortAPromRead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsIsa, pcnetIoPortWrite, pcnetIoPortRead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
    }
    else
        Assert(pThis->hIoPortsIsa == NIL_IOMIOPORTHANDLE);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePCNet =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "pcnet",
#ifdef PCNET_GC_ENABLED
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
#else
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS,
#endif
    /* .fClass = */                 PDM_DEVREG_CLASS_NETWORK,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(PCNETSTATE),
    /* .cbInstanceCC = */           sizeof(PCNETSTATECC),
    /* .cbInstanceRC = */           sizeof(PCNETSTATERC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "AMD PCnet Ethernet controller.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           pcnetR3Construct,
    /* .pfnDestruct = */            pcnetR3Destruct,
    /* .pfnRelocate = */            pcnetR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               pcnetR3Reset,
    /* .pfnSuspend = */             pcnetR3Suspend,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              pcnetR3Attach,
    /* .pfnDetach = */              pcnetR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            pcnetR3PowerOff,
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
    /* .pfnConstruct = */           pcnetRZConstruct,
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
    /* .pfnConstruct = */           pcnetRZConstruct,
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

