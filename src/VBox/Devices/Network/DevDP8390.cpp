/* $Id: DevDP8390.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevDP8390 - National Semiconductor DP8390-based Ethernet Adapter Emulation.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

/** @page pg_dev_dp8390 NatSemi DP8390-Based Ethernet NIC Emulation.
 *
 * This software was written based on the following documents:
 *
 *     - National Semiconductor DP8390/NS32490 Network Interface Controller,
 *          1986
 *     - National Semiconductor DP8390D/NS32490D NIC Network Interface
 *          Controller datasheet, July 1995
 *     - National Semiconductor Application Note 729, DP839EB-ATN IBM PC-AT
 *          Compatible DP83901 SNIC Serial Network Interface Controller
 *          Evaluation Board, 1993
 *     - National Semiconductor Application Note 842, The Design and Operation
 *          of a Low Cost, 8-Bit PC-XT Compatible Ethernet Adapter Using
 *          the DP83902, May 1993
 *     - National Semiconductor Application Note 858, Guide to Loopback Using
 *          the DP8390 Chip Set, October 1992
 *     - National Semiconductor Application Note 875, DP83905EB-AT AT/LANTIC
 *          Evaluation Board, June 1993
 *     - Western Digital WD83C584 Bus Interface Controller Device datasheet,
 *          October 29, 1990
 *     - Western Digital WD83C690 Ethernet LAN Controller datasheet,
 *          November 2, 1990
 *     - 3Com EtherLink II Adapter Technical Reference Manual,
 *          March 1991
 *
 * This emulation is compatible with drivers for:
 *  - Novell/Eagle/Anthem NE1000 (8-bit)
 *  - Novell/Eagle/Anthem NE2000 (16-bit)
 *  - Western Digital/SMC WD8003E (8-bit)
 *  - Western Digital/SMC WD8013EBT (16-bit)
 *  - 3Com EtherLink II 3C503 (8-bit)
 *
 *
 * The National Semiconductor DP8390 was an early (circa 1986) low-cost
 * Ethernet controller, typically accompanied by the DP8391 Serial Network
 * Interface and the DP8392 Coaxial Transceiver Interface.
 *
 * Due to its relatively low cost, the DP8390 NIC was chosen for several
 * very widespread early PC Ethernet designs, namely the Novell NE1000/NE2000,
 * Western Digital (later SMC) WD8003 EtherCard Plus, and 3Com EtherLink II.
 * The popularity of these cards, especially the NE2000, in turn spawned
 * a bevy of compatible chips from National Semiconductor and many others.
 *
 * All common DP8390-based cards have onboard memory. The initial WD8003E and
 * NE1000 cards have one 8Kx8 SRAM; 16-bit cards like WD8013E or NE2000 have
 * two 8Kx8 SRAMs wired in 8Kx16 configuration to enable 16-bit wide transfers.
 * The DP8390 can address up to 64K or local memory and uses "Local DMA"
 * (similar to bus mastering) to access it. Some newer cards had 32K or more
 * onboard RAM. Note that an NE2000 in 8-bit mode can only address 8K local
 * memory, effectively reverting to an NE1000.
 *
 * The DP8390 uses "Remote DMA" to move data between local memory and the host
 * system. Remote DMA is quite similar to 8237-style third party DMA, except
 * the DMA controller is on the DP8390 chip in this case.
 *
 * The DP8390 has a control bit (DCR.WTS) which selects whether all DMA (both
 * Local and Remote) transfers are 8-bit or 16-bit. Word-wide transfers can
 * generally only be used on a 16-bit card in a 16-bit slot, because only then
 * can the host drive 16-bit I/O cycles to the data ports. That is why
 * an NE2000 in an 8-bit slot can only use half of its local RAM -- remote DMA
 * simply cannot access half of the 8Kx16 SRAM.
 *
 * The DP8390 maps its internal registers as sixteen 8-bit wide I/O ports.
 * There are four register pages, selectable through the Command Register (CR)
 * which is accessible at offset 0 in all pages.
 *
 * The NE1000/NE2000 cards only use I/O and IRQ resources, not memory
 * or DMA. In contrast, the Western Digital cards use memory-mapped buffers.
 * Later AT/LANTIC (DP83905) based NE2000-compatible cards can optionally
 * use memory as well. The 3Com EtherLink II (3C503) uses a custom gate array
 * in addition to the DP8390 and can use programmed I/O, 8237 DMA, as well
 * as optional direct memory mapping.
 *
 * Address decoding is typically incomplete, which causes the buffer RAM and
 * possibly PROM to be aliased multiple times in the DP8390's address space.
 *
 * Buffer overflow handling is slightly tricky. The DP8390 assumes that if
 * the receiver is enabled, there is space for at least one page (256 bytes).
 * Once it fills up the page and advances the CURR pointer, the DP8390 checks
 * whether CURR equals BNRY and if so, triggers an overflow condition. Note
 * that after the NIC is initialized, CURR *will* normally equal BNRY, with
 * both pointing at the beginning of the receive ring (PSTART). An overflow
 * is only triggered when CURR equals BNRY right after advancing.
 *
 * The documentation of the Send Packet command mentions that when CRDA crosses
 * the PSTOP register, the current remote DMA address (i.e. CRDA) is set to
 * the PSTART value, which is rather convenient when reading received packets
 * out of the ring buffer using remote DMA. The documentation does not mention
 * that the same logic applies for all remote DMA reads, a feature that several
 * NE1000/NE2000 drivers (packet drivers, Novell ODI) rely on. This is logical,
 * because reading out of the receive ring buffer address range always implies
 * reading received packets, and then the PSTOP->PSTART wraparound becomes
 * desirable. It is unclear whether the same wraparound handling also applies
 * for remote DMA writes within the receive ring buffer.
 *
 * The documentation is not very clear on how the CRDA register is managed.
 * One might be led to believe that starting remote DMA copies the remote DMA
 * start address (i.e. RSAR) to the CRDA register. However, the NE1000 ODI
 * driver for OS/2 1.0 (NE1000.SYS from early 1988) relies on restarting remote
 * DMA and continuing where it left off. The DP8390D datasheet only mentions
 * this in a passing fashion at the end of the "Remote Write with High Speed
 * Buses" section, saying that if a dummy remote read is executed before a
 * remote write, RSAR can be set up for the dummy read such that the CRDA
 * register contains the desired value for the following write.
 *
 * Conversely, it is not spelled out that writing RSAR also updates CRDA, but
 * at least Novell's NE2000 ODI driver v2.12 is known to rely on this behavior
 * and checks that a write to RSAR is reflected in CRDA.
 *
 * Loopback operation is limited in the DP8390. Because it is a half-duplex
 * device, it cannot truly transmit and receive simultaneously. When loopback
 * is in effect, the received data is *not* written into memory. Only the last
 * few bytes of the packet are visible in the FIFO.
 *
 * Likewise due to its half-duplex nature, the CRC circuitry during loopback
 * works either only on the transmit side (FCS is generated but not checked)
 * or the receive side (FCS is checked but not generated).
 *
 * The loopback behavior is even stranger when DCR.WTS is set to enabled 16-bit
 * DMA transfers. Even though the chip reads 16 bits at a time, only 8 bits are
 * actually transmitted; the DCR.BOS bit determines whether the low or high
 * 8 bits of each words are transmitted. As a consequence, the programmed length
 * of the transmit is also halved.
 *
 * Because loopback operation is so different from normal send/receive, loopback
 * packets are not run through the normal receive path and are treated specially
 * instead. The WD and especially 3C503 diagnostics exercise the loopback
 * functionality fairly thoroughly.
 *
 *
 * NE1000 and NE2000
 * -----------------
 *
 * Common NE1000/NE2000 configurations in Novell drivers:
 *   I/O Base = 300h, IRQ = 3 (default)
 *   I/O Base = 320h, IRQ = 2
 *   I/O Base = 340h, IRQ = 4
 *   I/O Base = 360h, IRQ = 5
 * The I/O base can be set to 300h/320h/340h/360h; the IRQ to 2, 3, 4, 5.
 * No memory or DMA is used.
 *
 * The NE1000/NE2000 adds a data register and a reset register to the I/O
 * space. A PROM containing the node address is mapped into the DP8390's local
 * address space.
 *
 * The mapping of the 32x8 PROM on an NE2000 card is quite non-obvious but
 * fortunately well explained in the AN-729 Application Note. Address lines
 * A4-A1 of the internal bus are connected to lines A3-A0 of the PROM
 * (enabling 16 distinct bytes of the 32-byte PROM to be addressed). However,
 * the negated EN16 signal, which is active when the NE2000 is in a 16-bit
 * slot, is connected to the PROM's address line A4. That means an NE2000 in
 * a 16-bit slot reads different PROM bytes than when the same card is in an
 * 8-bit slot. The PROM is structured such that an NE2000 in an 8-bit slot
 * reads a 'BB' signature (same as NE1000) at PROM offset 1Eh/1Fh, while
 * an NE2000 in a 16-bit slot returns a 'WW' signature from PROM offset
 * 0Eh/0Fh instead.
 *
 * The original NE1000 boards Assy. #950-054401 actually only had 6 bytes of
 * MAC address in the PROM, the rest was unused (0FFh). Software supporting the
 * NE1000 thus should not examine the PROM contents beyond the first 6 bytes.
 *
 * Novell's old OUI was 00:00:D8 but drivers are not known to check for it.
 *
 * Newer DP83905 AT/LANTIC based NE2000plus cards were optionally capable of
 * using shared RAM in a manner very similar to the WD8003/WD8013.
 *
 *
 * WD8003 and WD8013 EtherCard Plus
 * --------------------------------
 *
 * Common WD8013 configurations:
 *   I/O Base = 280h, IRQ = 3,  RAM D000-D3FF (default)
 *   I/O Base = 330h, IRQ = 10, RAM CC00-CFFF
 *   I/O Base = 240h, IRQ/RAM soft-configurable
 * The I/O base can be set anywhere in the 2xxh-3xxh range in 20h increments.
 * The IRQs available on a WD8013 are 2, 3, 4, 5, 7, 10, 11, 15. The shared
 * RAM can be anywhere between 80000h (512K) to FFC000h (16M-16K) in 16K
 * increments.
 *
 * The Western Digital WD8003E appeared at around the same time as Novell's
 * NE1000 (1987). It is likewise a short 8-bit ISA card with 8Kx8 onboard
 * SRAM. The major difference is that rather than using remote DMA to move
 * data between the host and local RAM, the WD8003 directly mapps the onboard
 * memory to the host's address space (often called shared memory). A later
 * 16-bit WD8013 model used 8Kx16 SRAM, and there were follow-on WD8003 models
 * with 16K or 32K local RAM.
 *
 * Instead of mapping the PROM into the DP8390's local address space, the
 * WD8003/WD8013 exposes the node address through the I/O space; the DP8390's
 * local address space only contains buffer RAM.
 *
 * The WD8003 cannot use remote DMA at all; the host must use shared memory.
 * Remote DMA can be programmed but there is no way to trigger RDMA transfers.
 *
 * Western Digital's brand name for WD8003/WD8013 was EtherCard. Circa 1991,
 * WD sold the networking business to SMC; SMC continued to sell and further
 * develop the cards under the Elite brand name, also designated as the
 * SMC8000 series.
 *
 * The original WD8003E/EBT/WT uses very simple glue logic around the DP8390
 * and must be configured through jumpers. Newer WD8003EB/EP/EW/W/WC uses an
 * interface chip (WD83C583, WD83C584, or later) with an EEPROM and can be
 * configured through a software utility.
 *
 * Similarly the 16-bit WD8013EBT is configured only though jumpers, while
 * the newer WD8013EB/W/EW/EWC/WC/EPC are software configurable.
 *
 * The "Board ID" byte (at offset 6 in the PROM) is used to distinguish
 * between the various models.
 *
 * Newer WD cards use the WD83C690 controller rather than DP8390. The
 * WD83C690 is close enough to DP8390 that old WD drivers should work with
 * it, but it has a number of differences. It has no support for Remote DMA
 * whatsoever, and does not implement multicast filtering.
 *
 * The WD83C690 also handles receive buffer overflows somewhat differently;
 * the DP8390 never fills the last remaining buffer page, meaning that
 * CURR=BNRY indicates an empty buffer while CURR=BNRY-1 means buffer full.
 * The WD83C690 can fill all pages and decides whether it is full or empty
 * based on whether CURR or BNRY was changed more recently.
 *
 * Old Western Digital utilities/drivers may require the card to have WD's
 * old OUI of 00:00:0C and refuse to recognize the hardware otherwise.
 *
 * The emulation passes WD diagnostics with no errors (DIAGNOSE.EXE Ver 1.11,
 * dated 12/12/1989).
 *
 *
 * 3C503 EtherLink II
 * ------------------
 *
 * Common 3C503 configurations in Novell drivers:
 *   I/O Base = 300h, IRQ = 3 (default)
 * The I/O base can be set via jumpers to 2E0h, 2A0h, 280h, 250h, 350h, 330h,
 * 310h, or 300h (default). The ROM/RAM can be optionally mapped to one of
 * DC000-DFFFF, D8000-DBFFF, CC000-CFFFF, or C8000-CBFFF, again configured
 * through jumpers. The available IRQs are 2, 3, 4, or 5, and DRQs 1, 2, or 3,
 * both soft-configurable (no IRQ/DRQ jumpers).
 *
 * Yet another design based on the DP8390 was the 3Com 3C503 EtherLink II,
 * available sometime in 1988. Unlike Novell and WD, 3Com added a custom
 * host interface ASIC ("Gate Array") which handles all transfers to and from
 * the 8Kx8 onboard SRAM. The 3C503 can map the card's local RAM directly
 * into the host's address space, alternatively software can use either PIO
 * or 8-bit DMA to transfer data.
 *
 * For reasons that are not entirely clear, 3Com decided that the Remote DMA
 * implementation on the DP3890 (successfully used by the NE1000/NE2000) was
 * too buggy and the Gate Array essentially duplicates the Remote DMA
 * functionality, while also adding 8327 style DMA support (like the DP839EB
 * had) and optional shared RAM.
 *
 * Just like the NE1000/NE2000 and WD8003/WD8013, the 3C503 exists in an
 * 8-bit variant (EtherLink II) and a 16-bit variant (EtherLink II/16),
 * although both types are called 3C503.
 *
 * Since the 3C503 does not require shared RAM to operate, 3Com decided to
 * use a single memory mapping for both a boot ROM (if present) and shared
 * RAM. It is possible to boot from the ROM utilizing PIO or DMA for data
 * transfers, and later switch to shared RAM. However, 3Com needed to add
 * a hack for warm boot; the Vector Pointer Registers (VPTR0/1/2) contain
 * a 20-bit address and the Gate Array monitors the ISA bus for a read cycle
 * to that address. When a read cycle from the VPTR address occurs, the
 * memory mapping is switched from RAM to ROM. The VPTR registers are meant
 * to be programmed with the warm boot vector (often F000:FFF0 or FFFF0h).
 *
 * Some UNIX 3C503 drivers may require the card to have 3Com's old OUI
 * of 02:60:8C and refuse to detect the hardware otherwise. Likewise the
 * 3C503 diagnostics fail if the OUI is not 3Com's.
 *
 * The emulation passes 3Com diagnostics with flying colors (3C503.EXE Version
 * 1.5, dated 11/26/1991).
 *
 *
 * Linux Drivers
 *
 * The DP8390 driver (shared by NE1000/NE2000, WD8003/WD8013, and 3C503 drivers)
 * in Linux has severe bugs in the receive path. The driver clears receive
 * interrupts *after* going through the receive ring; that causes it to race
 * against the DP8390 chip and sometimes dismiss receive interrupts without
 * handling them. The driver also only receives at most 9 packets at a time,
 * which again can cause already received packets to be "hanging" in the receive
 * queue without the driver processing them.
 * In addition, prior to Linux 1.3.47, the driver incorrectly cleared the
 * overflow warning interrupt after any receive, causing it to potentially
 * miss overflow interrupts.
 *
 * The above bugs cause received packets to be lost or retransmitted by sender,
 * causing major TCP/IP performance issues when the DP8390 receives packets
 * very quickly. Other operating systems do not exhibit these bugs.
 *
 *
 * BSD Drivers
 *
 * For reasons that are not obvious, BSD drivers have configuration defaults far
 * off from the hardware defaults. For NE2000 (ne1), it is I/O base 300h and
 * IRQ 10. For WD8003E (we0), it is I/O base 280h, IRQ 9, memory D0000-D1FFF.
 * For 3C503 (ec0), it is I/O base 250h, IRQ 9, memory D8000-D9FFF (no DMA).
 *
 * The resource assigments are difficult to configure (sometimes impossible on
 * installation CDs) and the high IRQs may clash with PCI devices.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_DP8390
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

#define DPNIC_SAVEDSTATE_VERSION        1

/** Maximum number of times we report a link down to the guest (failure to send frame) */
#define DPNIC_MAX_LINKDOWN_REPORTED     3

/** Maximum number of times we postpone restoring a link that is temporarily down. */
#define DPNIC_MAX_LINKRST_POSTPONED     3

/** Maximum frame size we handle */
#define MAX_FRAME                       1536

/* Size of the local RAM. */
#define DPNIC_MEM_SIZE  16384u

#define DPNIC_MEM_MASK  (DPNIC_MEM_SIZE - 1)

/* Although it is a 16-bit adapter, the EtherLink II only supports 8-bit DMA
 * and therefore DMA channels 1 to 3 are available.
 */
#define ELNKII_MIN_VALID_DMA    1
#define ELNKII_MAX_VALID_DMA    3

/* EtherLink II Gate Array revision. */
#define ELNKII_GA_REV           1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/**
 * Emulated device types.
 */
enum DP8390_DEVICE_TYPE
{
    DEV_NE1000      = 0,    /* Novell NE1000 compatible (8-bit). */
    DEV_NE2000      = 1,    /* Novell NE2000 compatible (16-bit). */
    DEV_WD8003      = 2,    /* Western Digital WD8003 EtherCard Plus compatible (8-bit). */
    DEV_WD8013      = 3,    /* Western Digital WD8013 EtherCard Plus compatible (16-bit). */
    DEV_3C503       = 4     /* 3Com 3C503 EtherLink II compatible. */
};

/** WD8003/WD80013 specific register offsets. */
#define WDR_CTRL1       0   /* Control register 1. */
#define WDR_ATDET       1   /* 16-bit slot detect. */
#define WDR_IOBASE      2   /* I/O base register. */
#define WDR_CTRL2       5   /* Control register 2. */
#define WDR_JP          6   /* Jumper settings. */
#define WDR_PROM        8   /* PROM offset in I/O space. */

/** WD8013 Control Register 1. */
typedef struct WD_CTRL1 {
    uint8_t     A13_18 : 6; /* Shared memory decoding A13-A18. */
    uint8_t     MEME   : 1; /* Enable memory access. */
    uint8_t     RESET  : 1; /* Reset NIC core. */
} WD_CTRL1;
AssertCompile(sizeof(WD_CTRL1) == sizeof(uint8_t));

/** WD8013 Control Register 2. */
typedef struct WD_CTRL2 {
    uint8_t     A19_23 : 5; /* Shared memory decoding A19-A23. */
    uint8_t     res    : 1; /* Reserved. */
    uint8_t     MEMW   : 1; /* Memory width (16-bit wide if set). */
    uint8_t     M16    : 1; /* Allow 16-bit host memory cycles if set. */
} WD_CTRL2;
AssertCompile(sizeof(WD_CTRL2) == sizeof(uint8_t));


/** 3C503 EtherLink II specific register offsets. */
#define GAR_PSTR        0
#define GAR_PSPR        1
#define GAR_DQTR        2
#define GAR_R_BCFR      3
#define GAR_R_PCFR      4
#define GAR_GACFR       5
#define GAR_GACR        6
#define GAR_STREG       7
#define GAR_IDCFR       8
#define GAR_DAMSB       9
#define GAR_DALSB       10
#define GAR_VPTR2       11
#define GAR_VPTR1       12
#define GAR_VPTR0       13
#define GAR_RFMSB       14
#define GAR_RFLSB       15

/** 3C503 EtherLink II Gate Array registers. */

/** Gate Array DRQ Timer Register. */
typedef struct EL_DQTR {
    uint8_t     tb  : 5;    /* Timer bits; should be multiple of 4. */
    uint8_t     res : 3;    /* Reserved. */
} GA_DQTR;
AssertCompile(sizeof(GA_DQTR) == sizeof(uint8_t));

/** Gate Array Configuration Register. */
typedef struct EL_GACFR {
    uint8_t     mbs  : 3;   /* Memory Bank Select. */
    uint8_t     rsel : 1;   /* RAM Select. */
    uint8_t     test : 1;   /* Makes GA counters run at 10 MHz. */
    uint8_t     ows  : 1;   /* 0 Wait State for Gate Array. */
    uint8_t     tcm  : 1;   /* Terminal Count Mask for DMA (block interrupt if set). */
    uint8_t     nim  : 1;   /* NIC Interrupt Mask (block interrupt if set). */
} GA_GACFR;
AssertCompile(sizeof(GA_GACFR) == sizeof(uint8_t));

/** Gate Array Configuration Register. */
typedef struct EL_GACR {
    uint8_t     rst   : 1;  /* Hard reset GA/NIC. */
    uint8_t     xsel  : 1;  /* Transceiver Select. */
    uint8_t     ealo  : 1;  /* Window low 16 bytes of PROM to I/O space. */
    uint8_t     eahi  : 1;  /* Window high 16 bytes of PROM to I/O space. */
    uint8_t     share : 1;  /* Enable interrupt sharing. */
    uint8_t     dbsel : 1;  /* Double Buffer Select for FIFOs. */
    uint8_t     ddir  : 1;  /* DMA Direction (1=host to adapter). */
    uint8_t     start : 1;  /* Start Gate Array DMA. */
} GA_GACR;
AssertCompile(sizeof(GA_GACR) == sizeof(uint8_t));

/** Gate Array Status Register. */
typedef struct EL_STREG {
    uint8_t     rev   : 3;  /* Gate Array Revision. */
    uint8_t     dip   : 1;  /* DMA In Progress. */
    uint8_t     dtc   : 1;  /* DMA Terminal Count. */
    uint8_t     oflw  : 1;  /* Data Overflow. */
    uint8_t     uflw  : 1;  /* Data Underflow. */
    uint8_t     dprdy : 1;  /* Data Port Ready. */
} GA_STREG;
AssertCompile(sizeof(GA_STREG) == sizeof(uint8_t));

/** Gate Array Interrupt/DMA Configuration. */
typedef struct EL_IDCFR {
    uint8_t     drq1 : 1;   /* Enable DRQ 1. */
    uint8_t     drq2 : 1;   /* Enable DRQ 2. */
    uint8_t     drq3 : 1;   /* Enable DRQ 3. */
    uint8_t     res  : 1;   /* Unused. */
    uint8_t     irq2 : 1;   /* Enable IRQ 2. */
    uint8_t     irq3 : 1;   /* Enable IRQ 3. */
    uint8_t     irq4 : 1;   /* Enable IRQ 4. */
    uint8_t     irq5 : 1;   /* Enable IRQ 5. */
} GA_IDCFR;
AssertCompile(sizeof(GA_IDCFR) == sizeof(uint8_t));

/** Current DMA Address. */
typedef struct EL_CDADR {
    uint8_t     cdadr_lsb;  /* Current DMA Address LSB. */
    uint8_t     cdadr_msb;  /* Current DMA Address MSB. */
} GA_CDADR;
AssertCompile(sizeof(GA_CDADR) == sizeof(uint16_t));

/** 3C503 Gate Array state. */
typedef struct EL_GA_s {
    uint8_t         PSTR;       /* Page Start Register. */
    uint8_t         PSPR;       /* Page Stop Register. */
    union {
        uint8_t     DQTR;       /* DRQ Timer Register. */
        GA_DQTR     dqtr;
    };
    uint8_t         BCFR;       /* Base Configuration Register (R/O). */
    uint8_t         PCFR;       /* Boot PROM Configuration Register (R/O). */
    union {
        uint8_t     GACFR;
        GA_GACFR    gacfr;      /* Gate Array Configuration Register. */
    };
    union {
        uint8_t     GACR;       /* Gate Array Control Register. */
        GA_GACR     gacr;
    };
    union {
        uint8_t     STREG;      /* Gate Array Status Register (R/O). */
        GA_STREG    streg;
    };
    union {
        uint8_t     IDCFR;      /* Interrupt/DMA Configuration Register. */
        GA_IDCFR    idcfr;
    };
    uint8_t         DAMSB;      /* DMA Address MSB. */
    uint8_t         DALSB;      /* DMA Address LSB. */
    uint8_t         VPTR2;      /* Vector Pointer 2. */
    uint8_t         VPTR1;      /* Vector Pointer 1. */
    uint8_t         VPTR0;      /* Vector Pointer 0. */
    union {
        uint16_t    CDADR;      /* Current DMA address (internal state). */
        GA_CDADR    cdadr;
    };
    bool            fGaIrq;     /* Gate Array IRQ (internal state). */
} EL_GA, *PEL_GA;

/** DP8390 core register offsets. */
#define DPR_CR          0

#define DPR_P0_R_CLDA0  1
#define DPR_P0_W_PSTART 1
#define DPR_P0_R_CLDA1  2
#define DPR_P0_W_PSTOP  2
#define DPR_P0_BNRY     3
#define DPR_P0_R_TSR    4
#define DPR_P0_W_TPSR   4
#define DPR_P0_R_NCR    5
#define DPR_P0_W_TBCR0  5
#define DPR_P0_R_FIFO   6
#define DPR_P0_W_TBCR1  6
#define DPR_P0_ISR      7
#define DPR_P0_R_CRDA0  8
#define DPR_P0_W_RSAR0  8
#define DPR_P0_R_CRDA1  9
#define DPR_P0_W_RSAR1  9
#define DPR_P0_W_RBCR0  10
#define DPR_P0_W_RBCR1  11
#define DPR_P0_R_RSR    12
#define DPR_P0_W_RCR    12
#define DPR_P0_R_CNTR0  13
#define DPR_P0_W_TCR    13
#define DPR_P0_R_CNTR1  14
#define DPR_P0_W_DCR    14
#define DPR_P0_R_CNTR2  15
#define DPR_P0_W_IMR    15

#define DPR_P1_CURR     7

#define DPR_P2_R_PSTART 1
#define DPR_P2_W_CLDA0  1
#define DPR_P2_R_PSTOP  2
#define DPR_P2_W_CLDA1  2
#define DPR_P2_RNXTPP   3   /* Remote Next Packet Pointer. */
#define DPR_P2_R_TPSR   4
#define DPR_P2_LNXTPP   5   /* Local Next Packet Pointer. */
#define DPR_P2_ADRCU    6   /* Address Counter (Upper). */
#define DPR_P2_ADRCL    7   /* Address Counter (Lower). */
#define DPR_P2_R_RCR    12
#define DPR_P2_R_TCR    13
#define DPR_P2_R_DCR    14
#define DPR_P2_R_IMR    15


/** DP8390 Packet Header. */
typedef struct DP_PKT_HDR {
    uint8_t     rcv_stat;   /* Receive Status. */
    uint8_t     next_ptr;   /* Next Packet Pointer. */
    uint16_t    byte_cnt;   /* Receive byte count. */
} DP_PKT_HDR;

/** Select values for CR.RD field. */
#define DP_CR_RDMA_INVL 0   /* Invalid value. */
#define DP_CR_RDMA_RD   1   /* Remote Read. */
#define DP_CR_RDMA_WR   2   /* Remote Write. */
#define DP_CR_RDMA_SP   3   /* Send Packet. */
#define DP_CR_RDMA_ABRT 4   /* Abort Remote DMA. */

/** DP8390 Command Register (CR). */
typedef struct DP_CR {
    uint8_t     STP : 1;    /* Stop. */
    uint8_t     STA : 1;    /* Start. */
    uint8_t     TXP : 1;    /* Transmit Packet. */
    uint8_t     RD  : 3;    /* Remote DMA Command. */
    uint8_t     PS  : 2;    /* Page Select. */
} DP_CR;
AssertCompile(sizeof(DP_CR) == sizeof(uint8_t));

/** DP8390 Interrupt Status Register (ISR). */
typedef struct DP_ISR {
    uint8_t     PRX : 1;    /* Packet Received. */
    uint8_t     PTX : 1;    /* Packet Transmitted. */
    uint8_t     RXE : 1;    /* Receive Error. */
    uint8_t     TXE : 1;    /* Transmit Error. */
    uint8_t     OVW : 1;    /* Overwrite Warning (no receive buffers). */
    uint8_t     CNT : 1;    /* Counter Overflow. */
    uint8_t     RDC : 1;    /* Remote DMA Complete. */
    uint8_t     RST : 1;    /* Reset Status. */
} DP_ISR;
AssertCompile(sizeof(DP_ISR) == sizeof(uint8_t));

/** DP8390 Interrupt Mask Register (IMR). */
typedef struct DP_IMR {
    uint8_t     PRXE : 1;   /* Packet Received Interrupt Enable. */
    uint8_t     PTXE : 1;   /* Packet Transmitted Interrupt Enable. */
    uint8_t     RXEE : 1;   /* Receive Error Interrupt Enable. */
    uint8_t     TXEE : 1;   /* Transmit Error Interrupt Enable. */
    uint8_t     OVWE : 1;   /* Overwrite Warning  Interrupt Enable. */
    uint8_t     CNTE : 1;   /* Counter Overflow Interrupt Enable. */
    uint8_t     RDCE : 1;   /* DMA Complete Interrupt Enable. */
    uint8_t     res  : 1;   /* Reserved. */
} DP_IMR;
AssertCompile(sizeof(DP_IMR) == sizeof(uint8_t));

/** DP8390 Data Configuration Register (DCR). */
typedef struct DP_DCR {
    uint8_t     WTS : 1;    /* Word Transfer Select. */
    uint8_t     BOS : 1;    /* Byte Order Select. */
    uint8_t     LAS : 1;    /* Long Address Select. */
    uint8_t     LS  : 1;    /* Loopback Select. */
    uint8_t     ARM : 1;    /* Auto-Initialize Remote. */
    uint8_t     FT  : 2;    /* Fifo Threshold Select. */
    uint8_t     res : 1;    /* Reserved. */
} DP_DCR;
AssertCompile(sizeof(DP_DCR) == sizeof(uint8_t));

/** Transmit Configuration Register (TCR). */
typedef struct DP_TCR {
    uint8_t     CRC  : 1;   /* Inhibit CRC. */
    uint8_t     LB   : 2;   /* Loopback Control. */
    uint8_t     ATD  : 1;   /* Auto Transmit Disable. */
    uint8_t     OFST : 1;   /* Collision Offset Enable. */
    uint8_t     res  : 3;   /* Reserved. */
} DP_TCR;
AssertCompile(sizeof(DP_TCR) == sizeof(uint8_t));

/** Transmit Status Register (TSR). */
typedef struct DP_TSR {
    uint8_t     PTX : 1;    /* Packet Transmitted. */
    uint8_t     DFR : 1;    /* Non-Deferred Transmission (reserved in DP83901A). */
    uint8_t     COL : 1;    /* Transmit Collided. */
    uint8_t     ABT : 1;    /* Transmit Aborted. */
    uint8_t     CRS : 1;    /* Carrier Sense Lost. */
    uint8_t     FU  : 1;    /* FIFO Underrun. */
    uint8_t     CDH : 1;    /* CD Heartbeat. */
    uint8_t     OWC : 1;    /* Out of Window Collision. */
} DP_TSR;
AssertCompile(sizeof(DP_TSR) == sizeof(uint8_t));

/** Receive Configuration Register (RCR). */
typedef struct DP_RCR {
    uint8_t     SEP  : 1;   /* Save Errored Packets. */
    uint8_t     AR   : 1;   /* Accept Runt Packets. */
    uint8_t     AB   : 1;   /* Accept Broadcast. */
    uint8_t     AM   : 1;   /* Accept Multicast. */
    uint8_t     PRO  : 1;   /* Promiscuous Physical. */
    uint8_t     MON  : 1;   /* Monitor Mode. */
    uint8_t     res  : 2;   /* Reserved. */
} DP_RCR;
AssertCompile(sizeof(DP_RCR) == sizeof(uint8_t));

/** Receive Status Register (RSR). */
typedef struct DP_RSR {
    uint8_t     PRX  : 1;   /* Packet Received Intact. */
    uint8_t     CRC  : 1;   /* CRC Error. */
    uint8_t     FAE  : 1;   /* Frame Alignment Error. */
    uint8_t     FO   : 1;   /* FIFO Overrun. */
    uint8_t     MPA  : 1;   /* Missed Packet. */
    uint8_t     PHY  : 1;   /* Physical/Multicast Address. */
    uint8_t     DIS  : 1;   /* Receiver Disabled. */
    uint8_t     DFR  : 1;   /* Deferring. */
} DP_RSR;
AssertCompile(sizeof(DP_RSR) == sizeof(uint8_t));

/** Transmit Byte Count Register. */
typedef struct DP_TBCR {
    uint8_t     TBCR0;
    uint8_t     TBCR1;
} DP_TBCR;
AssertCompile(sizeof(DP_TBCR) == sizeof(uint16_t));

/** Current Local DMA Address. */
typedef struct DP_CLDA {
    uint8_t     CLDA0;
    uint8_t     CLDA1;
} DP_CLDA;
AssertCompile(sizeof(DP_CLDA) == sizeof(uint16_t));

/** Remote Start Address Register. */
typedef struct DP_RSAR {
    uint8_t     RSAR0;
    uint8_t     RSAR1;
} DP_RSAR;
AssertCompile(sizeof(DP_RSAR) == sizeof(uint16_t));

/** Remote Byte Count Register. */
typedef struct DP_RBCR {
    uint8_t     RBCR0;
    uint8_t     RBCR1;
} DP_RBCR;
AssertCompile(sizeof(DP_RBCR) == sizeof(uint16_t));

/** Current Remote DMA Address. */
typedef struct DP_CRDA {
    uint8_t     CRDA0;
    uint8_t     CRDA1;
} DP_CRDA;
AssertCompile(sizeof(DP_CRDA) == sizeof(uint16_t));

/** Page 1 registers. */
/* All registers read/write without side effects, unlike pages 0/2. */
typedef struct DP_PG1 {
    uint8_t     dummy_cr;
    uint8_t     PAR[6];     /* Physical Address PAR0-PAR5. */
    uint8_t     dummy_curr; /* Current Page Register. */
    uint8_t     MAR[8];     /* Multicast Address Register MAR0-MAR7. */
} DP_PG1;
AssertCompile(sizeof(DP_PG1) == 16);

/** DP8390 FIFO. Not all of the state is explicitly accessible. */
typedef struct DP_FIFO {
    uint8_t     rp;             /* Read pointer. */
    uint8_t     wp;             /* Write pointer. */
    uint8_t     fifo[16];       /* 16 bytes of FIFO. */
} DP_FIFO;

/**
 * Core DP8390 chip state.
 */
typedef struct DP8390CORE
{
    union {
        uint8_t     CR;         /* Command Register. */
        DP_CR       cr;
    };
    union {
        uint8_t     DCR;        /* Data Control Register. */
        DP_DCR      dcr;
    };
    /* Interrupt control. */
    union {
        uint8_t     ISR;        /* Interrupt Status Register. */
        DP_ISR      isr;
    };
    union {
        uint8_t     IMR;        /* Interrupt Mask Register. */
        DP_IMR      imr;
    };
    /* Receive state. */
    union {
        uint8_t     RCR;        /* Receive Control Register. */
        DP_RCR      rcr;
    };
    union {
        uint8_t     RSR;        /* Receive Status register. */
        DP_RSR      rsr;
    };
    /* Transmit State. */
    union {
        uint8_t     TCR;        /* Transmit Control Register. */
        DP_TCR      tcr;
    };
    union {
        uint8_t     TSR;        /* Transmit Status register. */
        DP_TSR      tsr;
    };
    uint8_t         NCR;        /* Number of Collisions Register. */
    /* Local DMA transmit state. */
    uint8_t         TPSR;       /* Transmit Page Start. */
    union {
        uint16_t    TBCR;       /* Transmit Byte Count. */
        DP_TBCR     tbcr;
    };
    /* Local DMA receive state. */
    union {
        uint16_t    CLDA;       /* Current Local DMA Address. */
        DP_CLDA     clda;
    };
    uint8_t         PSTART;     /* Page Start. */
    uint8_t         PSTOP;      /* Page Stop. */
    uint8_t         CURR;       /* Current Page. */
    uint8_t         BNRY;       /* Boundary Page. Also spelled BNDRY. */
    /* Remote DMA state. */
    union {
        uint16_t    RSAR;       /* Remote Start Address Register. */
        DP_RSAR     rsar;
    };
    union {
        uint16_t    RBCR;       /* Remote Byte Count Register. */
        DP_RBCR     rbcr;
    };
    union {
        uint16_t    CRDA;       /* Current Remote DMA Address. */
        DP_CRDA     crda;
    };
    /* Miscellaneous state. */
    uint8_t         lnxtpp;     /* Local Next Packet Pointer. */
    uint8_t         rnxtpp;     /* Remote Next Packet Pointer. */
    /* Tally counters. */
    uint8_t         CNTR0;      /* Frame Alignment Errors. */
    uint8_t         CNTR1;      /* CRC Errors. */
    uint8_t         CNTR2;      /* Missed Packet Errors. */
    union {
        uint8_t PG1[sizeof(DP_PG1)];
        DP_PG1      pg1;        /* All Page 1 Registers. */
    };
    DP_FIFO         fifo;       /* The internal FIFO. */
} DP8390CORE, *PDP8390CORE;

/**
 * DP8390-based card state.
 */
typedef struct DPNICSTATE
{
    /** Restore timer.
     *  This is used to disconnect and reconnect the link after a restore. */
    TMTIMERHANDLE                       hTimerRestore;

    /** Transmit signaller. */
    PDMTASKHANDLE                       hXmitTask;
    /** Receive ready signaller. */
    PDMTASKHANDLE                       hCanRxTask;

    /** Emulated device type. */
    uint8_t                             uDevType;
    /** State of the card's interrupt request signal. */
    bool                                fNicIrqActive;

    /** Core DP8390 chip state. */
    DP8390CORE                          core;

    /** WD80x3 Control Register 1. */
    union {
        uint8_t                         CTRL1;
        WD_CTRL1                        ctrl1;
    };
    /** WD80x3 Control Register 2. */
    union {
        uint8_t                         CTRL2;
        WD_CTRL2                        ctrl2;
    };

    /** 3C503 Gate Array state. */
    EL_GA                               ga;
    /** The 3C503 soft-configured ISA DMA channel. */
    uint8_t                             uElIsaDma;

    /** The PROM contents. 32 bytes addressable, R/O. */
    uint8_t                             aPROM[32];

    /** Shared RAM base. */
    RTGCPHYS                            MemBase;
    /** Shared RAM MMIO region handle. */
    PGMMMIO2HANDLE                      hSharedMem;
    /** Shared RAM size. */
    RTGCPHYS                            cbMemSize;

    /** Base port of the I/O space region. */
    RTIOPORT                            IOPortBase;
    /** The configured ISA IRQ. */
    uint8_t                             uIsaIrq;
    /** The configured ISA DMA channel. */
    uint8_t                             uIsaDma;
    /** If set the link is currently up. */
    bool                                fLinkUp;
    /** If set the link is temporarily down because of a saved state load. */
    bool                                fLinkTempDown;
    /** Number of times we've reported the link down. */
    uint16_t                            cLinkDownReported;
    /** Number of times we've postponed the link restore. */
    uint16_t                            cLinkRestorePostponed;

    /** The "hardware" MAC address. */
    RTMAC                               MacConfigured;

    /** Set if DPNICSTATER3::pDrv is not NULL. */
    bool                                fDriverAttached;
    /** The LED. */
    PDMLED                              Led;
    /** Status LUN: The LED ports. */
    PDMILEDPORTS                        ILeds;
    /** Partner of ILeds. */
    R3PTRTYPE(PPDMILEDCONNECTORS)       pLedsConnector;

    /** Access critical section. */
    PDMCRITSECT                         CritSect;
    /** Event semaphore for blocking on receive. */
    RTSEMEVENT                          hEventOutOfRxSpace;
    /** We are waiting/about to start waiting for more receive buffers. */
    bool volatile                       fMaybeOutOfSpace;

    /* MS to wait before we enable the link. */
    uint32_t                            cMsLinkUpDelay;
    /** The device instance number (for logging). */
    uint32_t                            iInstance;

    STAMCOUNTER                         StatReceiveBytes;
    STAMCOUNTER                         StatTransmitBytes;
#ifdef VBOX_WITH_STATISTICS
    STAMPROFILEADV                      StatIOReadRZ;
    STAMPROFILEADV                      StatIOReadR3;
    STAMPROFILEADV                      StatIOWriteRZ;
    STAMPROFILEADV                      StatIOWriteR3;
    STAMPROFILEADV                      StatReceive;
    STAMPROFILEADV                      StatTransmitR3;
    STAMPROFILEADV                      StatTransmitRZ;
    STAMPROFILE                         StatTransmitSendR3;
    STAMPROFILE                         StatTransmitSendRZ;
    STAMPROFILE                         StatRxOverflow;
    STAMCOUNTER                         StatRxOverflowWakeup;
    STAMCOUNTER                         StatRxCanReceiveNow;
    STAMCOUNTER                         StatRxCannotReceiveNow;
    STAMPROFILEADV                      StatInterrupt;
    STAMCOUNTER                         StatDropPktMonitor;
    STAMCOUNTER                         StatDropPktRcvrDis;
    STAMCOUNTER                         StatDropPktVeryShort;
    STAMCOUNTER                         StatDropPktVMNotRunning;
    STAMCOUNTER                         StatDropPktNoLink;
    STAMCOUNTER                         StatDropPktNoMatch;
    STAMCOUNTER                         StatDropPktNoBuffer;
#endif /* VBOX_WITH_STATISTICS */

    /** NIC-specific ISA I/O ports. */
    IOMIOPORTHANDLE                     hIoPortsNic;

    /** Common DP8390 core I/O ports. */
    IOMIOPORTHANDLE                     hIoPortsCore;

    /** The runt pad buffer (only really needs 60 bytes). */
    uint8_t                             abRuntBuf[64];

    /** The packet buffer. */
    uint8_t                             abLocalRAM[DPNIC_MEM_SIZE];

    /** The loopback transmit buffer (avoid stack allocations). */
    uint8_t                             abLoopBuf[DPNIC_MEM_SIZE];  /// @todo Can this be smaller?
} DPNICSTATE, *PDPNICSTATE;


/**
 * DP8390 state for ring-3.
 *
 * @implements  PDMIBASE
 * @implements  PDMINETWORKDOWN
 * @implements  PDMINETWORKCONFIG
 * @implements  PDMILEDPORTS
 */
typedef struct DPNICSTATER3
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
} DPNICSTATER3;
/** Pointer to a DP8390 state structure for ring-3. */
typedef DPNICSTATER3 *PDPNICSTATER3;


/**
 * DP8390 state for ring-0.
 */
typedef struct DPNICSTATER0
{
    /** Pointer to the connector of the attached network driver. */
    PPDMINETWORKUPR0                    pDrv;
} DPNICSTATER0;
/** Pointer to a DP8390 state structure for ring-0. */
typedef DPNICSTATER0 *PDPNICSTATER0;


/**
 * DP8390 state for raw-mode.
 */
typedef struct DPNICSTATERC
{
    /** Pointer to the connector of the attached network driver. */
    PPDMINETWORKUPRC                    pDrv;
} DPNICSTATERC;
/** Pointer to a DP8390 state structure for raw-mode. */
typedef DPNICSTATERC *PDPNICSTATERC;


/** The DP8390 state structure for the current context. */
typedef CTX_SUFF(DPNICSTATE) DPNICSTATECC;
/** Pointer to a DP8390 state structure for the current
 *  context. */
typedef CTX_SUFF(PDPNICSTATE) PDPNICSTATECC;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

static int dp8390CoreAsyncXmitLocked(PPDMDEVINS pDevIns, PDPNICSTATE pThis, PDPNICSTATECC pThisCC, bool fOnWorkerThread);

/**
 * Checks if the link is up.
 * @returns true if the link is up.
 * @returns false if the link is down.
 */
DECLINLINE(bool) dp8390IsLinkUp(PDPNICSTATE pThis)
{
    return pThis->fDriverAttached && !pThis->fLinkTempDown && pThis->fLinkUp;
}


/* Table and macro borrowed from DevPCNet.cpp. */
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


#ifndef ETHER_IS_MULTICAST /* Net/Open BSD macro it seems */
#define ETHER_IS_MULTICAST(a) ((*(uint8_t *)(a)) & 1)
#endif


/**
 * Check if incoming frame matches the station address.
 */
DECLINLINE(int) padr_match(PDPNICSTATE pThis, const uint8_t *buf)
{
    RTNETETHERHDR   *hdr = (RTNETETHERHDR *)buf;
    int             result;

    /* Checks own address only; always enabled if receiver on. */
    result = !memcmp(hdr->DstMac.au8, pThis->core.pg1.PAR, 6);

    return result;
}


/**
 * Check if incoming frame is an accepted broadcast frame.
 */
DECLINLINE(int) padr_bcast(PDPNICSTATE pThis, const uint8_t *buf)
{
    static uint8_t  aBCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    RTNETETHERHDR   *hdr = (RTNETETHERHDR *)buf;
    int result = pThis->core.rcr.AB && !memcmp(hdr->DstMac.au8, aBCAST, 6);
    return result;
}


/**
 * Check if incoming frame is an accepted multicast frame.
 */
DECLINLINE(int) padr_mcast(PDPNICSTATE pThis, const uint8_t *buf, int *mcast_type)
{
    uint32_t        crc = UINT32_MAX;
    RTNETETHERHDR   *hdr = (RTNETETHERHDR *)buf;
    int             result = 0;

    /* If multicast addresses are enabled, and the destination
     * address is in fact multicast, the address must be run through
     * the CRC generator and matched against the multicast filter
     * array.
     */
    if (pThis->core.rcr.AM && ETHER_IS_MULTICAST(hdr->DstMac.au8))
    {
        unsigned        i;
        const uint8_t   *p = buf;
        unsigned        crc_frag, crc_rev;
        unsigned        ma_bit_mask, ma_byte_idx;

        /* Indicate to caller that the address is a multicast one, regardless
         * of whether it's accepted or not.
         */
        *mcast_type = 1;

        for (i = 0; i < sizeof(hdr->DstMac); ++i)
            CRC(crc, *p++);

        /* The top 6 bits of the CRC calculated from the destination address
         * becomes an index into the 64-bit multicast address register. Sadly
         * our CRC algorithm is bit-reversed (Ethernet shifts bits out MSB first)
         * so instead of the top 6 bits of the CRC we have to take the bottom 6
         * and reverse the bits.
         */
        crc_frag  = crc & 63;

        for (i = 0, crc_rev = 0; i < 6; ++i)
            crc_rev |= ((crc_frag >> i) & 1) * (0x20 >> i);

        ma_bit_mask = 1 << (crc_rev & 7);
        ma_byte_idx = crc_rev / 8;
        Log3Func(("crc=%08X, crc_frag=%u, crc_rev=%u, ma_byte_idx=%u, ma_bit_mask=%02X\n", crc, crc_frag, crc_rev, ma_byte_idx, ma_bit_mask));
        Log3Func(("MAR: %02X:%02X:%02X:%02X %02X:%02X:%02X:%02X\n", pThis->core.pg1.MAR[0], pThis->core.pg1.MAR[1], pThis->core.pg1.MAR[2], pThis->core.pg1.MAR[3], pThis->core.pg1.MAR[4], pThis->core.pg1.MAR[5], pThis->core.pg1.MAR[6], pThis->core.pg1.MAR[7]));

        /* The multicast filtering logic is fairly extensively
         * verified by EtherLink II diagnostics (3C503.EXE).
         */
        if (pThis->core.pg1.MAR[ma_byte_idx] & ma_bit_mask)
        {
            Log3Func(("Passed multicast filter\n"));
            result = 1;
        }
    }

    return result;
}


/**
 * Check if incoming frame is an accepted promiscuous frame.
 */
DECLINLINE(int) padr_promi(PDPNICSTATE pThis, const uint8_t *buf)
{
    RTNETETHERHDR   *hdr = (RTNETETHERHDR *)buf;
    int result = pThis->core.rcr.PRO && !ETHER_IS_MULTICAST(hdr->DstMac.au8);
    return result;
}


/**
 * Update the device IRQ line based on internal state.
 */
static void dp8390CoreUpdateIrq(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    bool     fCoreIrqActive = false;
    bool     fNicIrqActive  = false;

    STAM_PROFILE_ADV_START(&pThis->StatInterrupt, a);

    /* Set the ISR.CNT bit based on the counter state (top counter bits ANDed together). */
    pThis->core.isr.CNT = (pThis->core.CNTR0 & pThis->core.CNTR1 & pThis->core.CNTR2) >> 7;

    /* IRQ is active if a bit is set in ISR and the corresponding bit
     * is set in IMR. No additional internal state needed.
     */
    Assert(!pThis->core.imr.res);
    if (pThis->core.ISR & pThis->core.IMR)
        fCoreIrqActive = true;

    /* The 3C503 has additional interrupt sources and control. For other device
     * types, the extras magically work out to be a no-op.
     */
    pThis->ga.fGaIrq = pThis->ga.streg.dtc && !pThis->ga.gacfr.tcm;
    fNicIrqActive = (fCoreIrqActive && !pThis->ga.gacfr.nim) || (pThis->ga.streg.dtc && !pThis->ga.gacfr.tcm);

    Log2Func(("#%d set irq fNicIrqActive=%d (fCoreIrqActive=%d, fGaIrq=%d)\n", pThis->iInstance, fNicIrqActive, fCoreIrqActive, pThis->ga.fGaIrq));

    /* The IRQ line typically does not change. */
    if (RT_UNLIKELY(fNicIrqActive != pThis->fNicIrqActive))
    {
        LogFunc(("#%d IRQ=%d, state=%d\n", pThis->iInstance, pThis->uIsaIrq, fNicIrqActive));
        /// @todo Handle IRQ 2/9 elsewhere
        PDMDevHlpISASetIrq(pDevIns, pThis->uIsaIrq == 2 ? 9 : pThis->uIsaIrq, fNicIrqActive);
        pThis->fNicIrqActive = fNicIrqActive;
    }
    STAM_PROFILE_ADV_STOP(&pThis->StatInterrupt, a);
}


/**
 * Perform a software reset of the NIC.
 */
static void dp8390CoreReset(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    LogFlowFunc(("#%d:\n", pThis->iInstance));

    /* DP8390 or DP83901A datasheet, section 11.0. */
    pThis->core.cr.TXP  = 0;
    pThis->core.cr.STA  = 0;
    pThis->core.cr.STP  = 1;
    pThis->core.cr.RD   = DP_CR_RDMA_ABRT;
    pThis->core.isr.RST = 1;
    pThis->core.IMR     = 0;
    pThis->core.dcr.LAS = 0;
    pThis->core.tcr.LB  = 0;

    /// @todo Check if this really happens on soft reset
    /* Clear the internal FIFO including r/w pointers. */
    memset(&pThis->core.fifo, 0, sizeof(pThis->core.fifo));

    /* Make sure the IRQ line us updated. */
    dp8390CoreUpdateIrq(pDevIns, pThis);
}

#ifdef IN_RING3

static DECLCALLBACK(void) dp8390R3WakeupReceive(PPDMDEVINS pDevIns)
{
    PDPNICSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    LogFlowFunc(("#%d\n", pThis->iInstance));
    STAM_COUNTER_INC(&pThis->StatRxOverflowWakeup);
    if (pThis->hEventOutOfRxSpace != NIL_RTSEMEVENT)
        RTSemEventSignal(pThis->hEventOutOfRxSpace);
}

/**
 * @callback_method_impl{FNPDMTASKDEV,
 * Signal to R3 that NIC is ready to receive a packet.
 */
static DECLCALLBACK(void) dpNicR3CanRxTaskCallback(PPDMDEVINS pDevIns, void *pvUser)
{
    RT_NOREF(pvUser);
    dp8390R3WakeupReceive(pDevIns);
}

#endif /* IN_RING3 */

/**
 * Read up to 256 bytes from a single page of local RAM.
 */
static void dpLocalRAMReadBuf(PDPNICSTATE pThis, uint16_t addr, unsigned cb, uint8_t *pDst)
{
    if ((RT_LOBYTE(addr) + cb) > 256)
    {
        LogFunc(("#%d: addr=%04X, cb=%X, cb!!\n", pThis->iInstance, addr, cb));
        cb = 256 - RT_LOBYTE(addr);
    }

    /* A single page is always either entirely inside or outside local RAM. */
    if (pThis->uDevType == DEV_NE1000)
    {
        /* Only 14 bits of address are decoded. */
        addr &= 0x3fff;
        if (addr >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            addr -= 0x2000;
            memcpy(pDst, &pThis->abLocalRAM[addr], cb);
        }
        else
            LogFunc(("#%d: Ignoring read at addr=%04X cb=%u!\n", pThis->iInstance, addr, cb));
    }
    else if (pThis->uDevType == DEV_NE2000)
    {
        /* Only 15 bits of address are decoded. */
        addr &= 0x7fff;
        if (addr >= 0x4000)
        {
            /* Local RAM is mapped at 4000h-7FFFh. */
            addr -= 0x4000;
            memcpy(pDst, &pThis->abLocalRAM[addr], cb);
        }
        else
            LogFunc(("#%d: Ignoring read at addr=%04X cb=%u!\n", pThis->iInstance, addr, cb));
    }
    else if ((pThis->uDevType == DEV_WD8003) || (pThis->uDevType == DEV_WD8013))
    {
        /* Local RAM is mapped starting at address zero. */
        addr &= DPNIC_MEM_MASK;
        if (addr + cb <= DPNIC_MEM_SIZE)
            memcpy(pDst, &pThis->abLocalRAM[addr], cb);
        else
            LogFunc(("#%d: Ignoring read at addr=%04X cb=%u!\n", pThis->iInstance, addr, cb));
    }
    else if (pThis->uDevType == DEV_3C503)
    {
        /* Only 14 bits of address are decoded. */
        /// @todo Is there any internal wrap-around in the 3C503 too?
        addr &= 0x3fff;
        if (addr >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            addr -= 0x2000;
            memcpy(pDst, &pThis->abLocalRAM[addr], cb);
        }
        else
            LogFunc(("#%d: Ignoring read at addr=%04X cb=%u!\n", pThis->iInstance, addr, cb));
    }
    else
    {
        Assert(0);
    }
}


#ifdef IN_RING3

/**
 * Write up to 256 bytes into a single page of local RAM.
 */
static void dpLocalRAMWriteBuf(PDPNICSTATE pThis, uint16_t addr, unsigned cb, const uint8_t *pSrc)
{
    if ((RT_LOBYTE(addr) + cb) > 256)
    {
        LogFunc(("#%d: addr=%04X, cb=%X, cb!!\n", pThis->iInstance, addr, cb));
        cb = 256 - RT_LOBYTE(addr);
    }

    /* A single page is always either entirely inside or outside local RAM. */
    if (pThis->uDevType == DEV_NE1000)
    {
        /* Only 14 bits of address are decoded. */
        addr &= 0x3fff;
        if (addr >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            addr -= 0x2000;
            memcpy(&pThis->abLocalRAM[addr], pSrc, cb);
        }
        else
            LogFunc(("#%d: Ignoring write at addr=%04X cb=%u!\n", pThis->iInstance, addr, cb));
    }
    else if (pThis->uDevType == DEV_NE2000)
    {
        /* Only 14 bits of address are decoded. */
        addr &= 0x7fff;
        if (addr >= 0x4000)
        {
            /* Local RAM is mapped at 4000h-7FFFh. */
            addr -= 0x4000;
            memcpy(&pThis->abLocalRAM[addr], pSrc, cb);
        }
        else
            LogFunc(("#%d: Ignoring write at addr=%04X cb=%u!\n", pThis->iInstance, addr, cb));
    }
    else if ((pThis->uDevType == DEV_WD8003) || (pThis->uDevType == DEV_WD8013))
    {
        /* Local RAM is mapped starting at address zero. */
        addr &= DPNIC_MEM_MASK;
        if (addr + cb <= DPNIC_MEM_SIZE)
            memcpy(&pThis->abLocalRAM[addr], pSrc, cb);
        else
            LogFunc(("#%d: Ignoring write at addr=%04X cb=%u!\n", pThis->iInstance, addr, cb));
    }
    else if (pThis->uDevType == DEV_3C503)
    {
        /* Only 14 bits of address are decoded. */
        /// @todo Is there any internal wrap-around in the 3C503 too?
        addr &= 0x3fff;
        if (addr >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            addr -= 0x2000;
            memcpy(&pThis->abLocalRAM[addr], pSrc, cb);
        }
        else
            LogFunc(("#%d: Ignoring write at addr=%04X cb=%u!\n", pThis->iInstance, addr, cb));
    }
    else
    {
        Assert(0);
    }
}


/**
 * Receive an arbitrarily long buffer into the receive ring starting at CLDA.
 * Update RSR, CLDA, and other state in the process.
 */
static void dp8390CoreReceiveBuf(PDPNICSTATE pThis, DP_RSR *pRsr, const uint8_t *src, unsigned cbLeft, bool fLast)
{
    LogFlow(("#%d: Initial CURR=%02X00 CLDA=%04X\n", pThis->iInstance, pThis->core.CURR, pThis->core.CLDA));

    while (cbLeft)
    {
        unsigned    cbWrite;
        unsigned    cbPage;

        /* Write at most up to the end of a page. */
        cbPage = cbWrite = 256 - pThis->core.clda.CLDA0;
        if (cbWrite > cbLeft)
            cbWrite = cbLeft;
        Log2Func(("#%d: cbLeft=%d CURR=%02X00 CLDA=%04X\n", pThis->iInstance, cbLeft, pThis->core.CURR, pThis->core.CLDA));
        dpLocalRAMWriteBuf(pThis, pThis->core.CLDA, cbWrite, src);
        src += cbWrite;

        /* If this is the last fragment of a received frame, we need to
         * round CLDA up to the next page boundary to correctly evaluate
         * buffer overflows and the next pointer. Otherwise we just
         * add however much data we had so that we can continue writing
         * at the CLDA position.
         */
        if (fLast && (cbWrite == cbLeft))
        {
            Log3Func(("#%d: Round up: CLDA=%04X cbPage=%X\n", pThis->iInstance, pThis->core.CLDA, cbPage));
            pThis->core.CLDA += cbPage;
        }
        else
            pThis->core.CLDA += cbWrite;

        Log3Func(("#%d: Final CURR=%02X00 CLDA=%04X\n", pThis->iInstance, pThis->core.CURR, pThis->core.CLDA));
        /* If at end of ring, wrap around. */
        if (pThis->core.clda.CLDA1 == pThis->core.PSTOP)
            pThis->core.clda.CLDA1 = pThis->core.PSTART;

        /* Check for buffer overflow. */
        if (pThis->core.clda.CLDA1 == pThis->core.BNRY)
        {
            pThis->core.isr.OVW = 1;
            pThis->core.isr.RST = 1;
            pRsr->MPA = 1;  /* Indicates to caller that receive was aborted. */
            STAM_COUNTER_INC(&pThis->StatDropPktNoBuffer);
            Log3Func(("#%d: PSTART=%02X00 PSTOP=%02X00 BNRY=%02X00 CURR=%02X00 -- overflow!\n", pThis->iInstance, pThis->core.PSTART, pThis->core.PSTOP, pThis->core.BNRY, pThis->core.CURR));
            break;
        }
        cbLeft -= cbWrite;
    }
}

/**
 * Write incoming data into the packet buffer.
 */
static void dp8390CoreReceiveLocked(PPDMDEVINS pDevIns, PDPNICSTATE pThis, const uint8_t *src, size_t cbToRecv)
{
    int is_padr = 0, is_bcast = 0, is_mcast = 0, is_prom = 0;
    int mc_type = 0;

    /*
     * Drop all packets if the VM is not running yet/anymore.
     */
    VMSTATE enmVMState = PDMDevHlpVMState(pDevIns);
    if (    enmVMState != VMSTATE_RUNNING
        &&  enmVMState != VMSTATE_RUNNING_LS)
    {
        STAM_COUNTER_INC(&pThis->StatDropPktVMNotRunning);
        return;
    }

    /*
     * Drop all packets if the cable is not connected.
     */
    if (RT_UNLIKELY(!dp8390IsLinkUp(pThis)))
    {
        STAM_COUNTER_INC(&pThis->StatDropPktNoLink);
        return;
    }

    /*
     * Drop everything if NIC is not started or in reset.
     */
    if (RT_UNLIKELY(!pThis->core.cr.STA || pThis->core.cr.STP))
    {
        STAM_COUNTER_INC(&pThis->StatDropPktRcvrDis);
        return;
    }

    /* Drop impossibly short packets. The DP8390 requires a packet to have
     * at least 8 bytes to even qualify as a runt. We can also assume that
     * there is a complete destination address at that point.
     */
    if (RT_UNLIKELY(cbToRecv < 8))
    {
        STAM_COUNTER_INC(&pThis->StatDropPktVeryShort);
        return;
    }

    LogFlowFunc(("#%d: size on wire=%d\n", pThis->iInstance, cbToRecv));

    /*
     * Perform address matching. Packets which do not pass any address
     * matching logic are ignored.
     */
    if (   (is_padr  = padr_match(pThis, src))
        || (is_bcast = padr_bcast(pThis, src))
        || (is_mcast = padr_mcast(pThis, src, &mc_type))
        || (is_prom  = padr_promi(pThis, src)))
    {
        union {
            uint8_t     nRSR;
            DP_RSR      nRsr;
        };
        uint32_t    fcs = 0;

        nRSR = 0;
        Log2Func(("#%d Packet passed address filter (is_padr=%d, is_bcast=%d, is_mcast=%d, is_prom=%d), size=%d\n", pThis->iInstance, is_padr, is_bcast, is_mcast, is_prom, cbToRecv));

        if (is_bcast || mc_type)
            nRsr.PHY = 1;

        /* In Monitor Mode, just increment the tally counter. */
        if (RT_UNLIKELY(pThis->core.rcr.MON))
        {
            STAM_COUNTER_INC(&pThis->StatDropPktMonitor);
            nRsr.MPA = 1;
            if (pThis->core.CNTR2 <= 192)
                pThis->core.CNTR2++;    /* Relies on UpdateIrq to be run. */
        }
        else
        {
            /* Error detection: FCS and frame alignment errors cannot happen,
             * likewise FIFO overruns can't.
             * Runts are padded up to the required minimum. Note that the DP8390
             * documentation considers packets smaller than 64 bytes to be runts,
             * but that includes 32 bits of FCS.
             */

            /* See if we need to pad, and how much. Note that if there's any
             * room left in the receive buffers, a runt will fit even after padding.
             */
            if (RT_UNLIKELY(cbToRecv < 60))
            {
                /// @todo This really is kind of stupid. We shouldn't be doing any
                /// padding here, it should be done by the sending side!
                memset(pThis->abRuntBuf, 0, sizeof(pThis->abRuntBuf));
                memcpy(pThis->abRuntBuf, src, cbToRecv);
                cbToRecv = 60;
                src = pThis->abRuntBuf;
            }

            LogFlowFunc(("#%d: PSTART=%02X00 PSTOP=%02X00 BNRY=%02X00 CURR=%02X00\n", pThis->iInstance, pThis->core.PSTART, pThis->core.PSTOP, pThis->core.BNRY, pThis->core.CURR));

            /* All packets that passed the address filter are copied to local RAM.
             * Since the DP8390 does not know how long the frame is until it detects
             * end of frame, it can only detect an out-of-buffer condition after
             * filling up all available space. It then reports an error and rewinds
             * back to where it was before.
             *
             * We do not limit the incoming frame size except by available buffer space. /// @todo Except we do??
             */

            STAM_REL_COUNTER_ADD(&pThis->StatReceiveBytes, cbToRecv);

            /* Copy incoming data to the packet buffer. Start by setting CLDA
             * to CURR + 4, leaving room for header.
             */
            pThis->core.CLDA = RT_MAKE_U16(4, pThis->core.CURR);

            /* Receive the incoming frame. */
            Assert(cbToRecv < MAX_FRAME);   /// @todo Can we actually do bigger?
            dp8390CoreReceiveBuf(pThis, &nRsr, src, (unsigned)cbToRecv, false);
            /// @todo Use the same method for runt padding?

            /* If there was no overflow, add the FCS. */
            if (!nRsr.MPA)
            {
                fcs = 0xBADF00D;    // Just fake it, does anyone care?
                dp8390CoreReceiveBuf(pThis, &nRsr, (uint8_t *)&fcs, sizeof(fcs), true);
            }

            /* Error-free packets are considered intact. */
            if (!nRsr.CRC && !nRsr.FAE && !nRsr.FO && !nRsr.MPA)
            {
                nRsr.PRX = 1;
                pThis->core.isr.PRX = 1;
            }
            else
                pThis->core.isr.RXE = 1;

            /* For 'intact' packets, write the packet header. */
            if (nRsr.PRX)
            {
                DP_PKT_HDR  header;

                /* Round up CLDA to the next page. */
                if (pThis->core.clda.CLDA0)
                    pThis->core.CLDA = RT_MAKE_U16(0, pThis->core.clda.CLDA1 + 1);

                /* If entire frame was successfully received, write the packet header at the old CURR. */
                header.rcv_stat = nRSR;
                header.next_ptr = pThis->core.clda.CLDA1;
                /// @todo big endian (WTS)
                header.byte_cnt = (uint16_t)cbToRecv + sizeof(fcs);

                pThis->core.CLDA = RT_MAKE_U16(0, pThis->core.CURR);
                dpLocalRAMWriteBuf(pThis, pThis->core.CLDA, sizeof(header), (uint8_t *)&header);
                pThis->core.CLDA += sizeof(header);

                pThis->core.CURR = header.next_ptr;
            }
        }

        pThis->core.RSR = nRSR;

        Log2Func(("Receive completed, size=%d, CURR=%02X00, RSR=%02X, ISR=%02X\n", cbToRecv, pThis->core.CURR, pThis->core.RSR, pThis->core.ISR));
        dp8390CoreUpdateIrq(pDevIns, pThis);
    }
    else
    {
        Log3Func(("#%d Packet did not pass address filter, size=%d\n", pThis->iInstance, cbToRecv));
        STAM_COUNTER_INC(&pThis->StatDropPktNoMatch);
    }
}

#endif /* IN_RING3 */


/**
 * Transmit a packet from local memory.
 *
 * @returns VBox status code.  VERR_TRY_AGAIN is returned if we're busy.
 *
 * @param   pDevIns             The device instance data.
 * @param   pThis               The device state data.
 * @param   fOnWorkerThread     Whether we're on a worker thread or on an EMT.
 */
static int dp8390CoreXmitPacket(PPDMDEVINS pDevIns, PDPNICSTATE pThis, bool fOnWorkerThread)
{
    PDPNICSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDPNICSTATECC);
    RT_NOREF_PV(fOnWorkerThread);
    int rc;

    /*
     * Grab the xmit lock of the driver as well as the DP8390 device state.
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
        /*
         * Do the transmitting.
         */
        int rc2 = dp8390CoreAsyncXmitLocked(pDevIns, pThis, pThisCC, false /*fOnWorkerThread*/);
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


#ifdef IN_RING3

/**
 * @callback_method_impl{FNPDMTASKDEV,
 * This is just a very simple way of delaying sending to R3.
 */
static DECLCALLBACK(void) dpNicR3XmitTaskCallback(PPDMDEVINS pDevIns, void *pvUser)
{
    PDPNICSTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    NOREF(pvUser);

    /*
     * Transmit if we can.
     */
    dp8390CoreXmitPacket(pDevIns, pThis, true /*fOnWorkerThread*/);
}

#endif /* IN_RING3 */


/**
 * Allocates a scatter/gather buffer for a transfer.
 *
 * @returns See PPDMINETWORKUP::pfnAllocBuf.
 * @param   pThis       The device instance.
 * @param   pThisCC     The device state for current context.
 * @param   cbMin       The minimum buffer size.
 * @param   fLoopback   Set if we're in loopback mode.
 * @param   pSgLoop     Pointer to stack storage for the loopback SG.
 * @param   ppSgBuf     Where to return the SG buffer descriptor on success.
 *                      Always set.
 */
DECLINLINE(int) dp8390XmitAllocBuf(PDPNICSTATE pThis, PDPNICSTATECC pThisCC, size_t cbMin, bool fLoopback,
                                   PPDMSCATTERGATHER pSgLoop, PPPDMSCATTERGATHER ppSgBuf)
{
    int rc;

    if (!fLoopback)
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
    else
    {
        /* Fake loopback allocator. */
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
    return rc;
}


/**
 * Sends the scatter/gather buffer.
 *
 * Wrapper around PDMINETWORKUP::pfnSendBuf, so check it out for the fine print.
 *
 * @returns See PDMINETWORKUP::pfnSendBuf.
 * @param   pDevIns         The device instance.
 * @param   pThisCC         The current context device state.
 * @param   fLoopback       Set if we're in loopback mode.
 * @param   pSgBuf          The SG to send.
 * @param   fOnWorkerThread Set if we're being called on a work thread.  Clear
 *                          if an EMT.
 */
DECLINLINE(int) dp8390CoreXmitSendBuf(PPDMDEVINS pDevIns, PDPNICSTATECC pThisCC, bool fLoopback, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int rc;
    STAM_REL_COUNTER_ADD(&pThis->StatTransmitBytes, pSgBuf->cbUsed);
    if (!fLoopback)
    {
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
    else
    {
        PDP8390CORE     pCore = &pThis->core;
        union {
            uint8_t     nRSR;
            DP_RSR      nRsr;
        };
        unsigned        ofs;
        uint32_t        fcs = UINT32_MAX;

        nRSR = 0;

        /* Loopback on the DP8390 is so strange that it must be handled specially. */
        Assert(pSgBuf->pvAllocator == (void *)pThis);
        pThis->Led.Asserted.s.fReading = pThis->Led.Actual.s.fReading = 1;

        LogFlowFunc(("#%d: loopback (DCR=%02X LB=%u TCR=%02X RCR=%02X, %u bytes)\n", pThis->iInstance, pCore->DCR, pCore->tcr.LB, pCore->TCR, pCore->RCR, pSgBuf->cbUsed));
        for (ofs = 0; ofs < pSgBuf->cbUsed; ofs += 16)
            Log(("  %04X: %.*Rhxs\n", ofs, ofs + 16 < pSgBuf->cbUsed ? 16 : pSgBuf->cbUsed - ofs, &pThis->abLoopBuf[ofs]));

        /* A packet shorter than 8 bytes is ignored by the receiving side. */
        if (pSgBuf->cbUsed < 8)
            return VINF_SUCCESS;

        /* The loopback mode affects transmit status bits. */
        switch (pCore->tcr.LB)
        {
        case 1: /* Internal loopback within DP8390. */
            pCore->tsr.CDH = 1;
            pCore->tsr.CRS = 1;
            break;
        case 2: /* Loopback through serializer. */
            pCore->tsr.CDH = 1;
            break;
        case 3: /* External loopback. Requires a cable. */
            break;
        default:
            Assert(0);
        }

        /* The CRC Inhibit controls whether transmit or receive path uses the
         * CRC circuitry. If transmit side uses CRC, receive always fails.
         * We always need to calculate the FCS because either the sending or
         * the receiving side uses it.
         */
        uint8_t     *p;
        uint8_t     *pktbuf = pThis->abLoopBuf; /// @todo Point into sgbuf instead?
        uint16_t    pktlen = (uint16_t)pSgBuf->cbUsed;
        uint16_t    fcslen = pktlen;
        uint8_t     abFcs[4];
        bool        fAddrMatched = true;

        /* If the receiver side is calculating FCS, it needs to skip the last
         * bytes (which are the transmit-side FCS).
         */
        if (pCore->tcr.CRC && (pktlen > 4))
            fcslen -= 4;

        p = pktbuf;
        while (p != &pktbuf[fcslen])
            CRC(fcs, *p++);

        fcs = ~fcs;
        Log3Func(("FCS: %08X\n", fcs));
        for (ofs = 0; ofs < sizeof(abFcs); ++ofs)
        {
            abFcs[ofs] = (uint8_t)fcs;
            fcs >>= 8;
        }

        /* The FIFO write pointer gets zeroed on each receive,
         * but the read pointer does not.
         */
        pCore->fifo.wp = 0;

        if (pCore->tcr.CRC)
        {
            bool    fGoodFcs = true;
            int     is_padr = 0, is_bcast = 0, is_mcast = 0, is_prom = 0;
            int     mc_type = 0;

            /* Always put the first 8 bytes of the packet in the FIFO. */
            for (ofs = 0; ofs < 8; ++ofs)
                pCore->fifo.fifo[pCore->fifo.wp++ & 7] = pktbuf[ofs];


            /* If the receiving side uses the CRC circuitry, it also performs
             * destination address matching.
             */
            if (   (is_padr  = padr_match(pThis, pktbuf))
                || (is_bcast = padr_bcast(pThis, pktbuf))
                || (is_mcast = padr_mcast(pThis, pktbuf, &mc_type))
                || (is_prom  = padr_promi(pThis, pktbuf)))
            {
                /* Receiving side checks the FCS. */
                fGoodFcs = !memcmp(&pktbuf[pktlen - 4], abFcs, sizeof(abFcs));
                Log2Func(("#%d: Address matched (is_padr=%d, is_bcast=%d, is_mcast=%d, is_prom=%d), checking FCS (fGoodFcs=%RTbool)\n", pThis->iInstance, is_padr, is_bcast, is_mcast, is_prom, fGoodFcs));

                /* Now we have to update the FIFO. Since only 8 bytes are visible
                 * in the FIFO after a receive, we can skip most of it.
                 */
                for ( ; ofs < pktlen; ++ofs)
                    pCore->fifo.fifo[pCore->fifo.wp++ & 7] = pktbuf[ofs];

            }
            else
            {
                nRsr.PRX = 1;   /* Weird but true, for non-matching address only! */
                fAddrMatched = false;
                Log3Func(("#%d: Address NOT matched, ignoring FCS errors.\n", pThis->iInstance));
            }

            /* The PHY bit is set when when an enabled broadcast packet is accepted,
             * but also when an enabled multicast packet arrives regardless of whether
             * it passes the MAR filter or not.
             */
            if (is_bcast || mc_type)
                nRsr.PHY = 1;

            if (!fGoodFcs)
                nRsr.CRC = 1;
        }
        else
        {
            nRsr.CRC = 1;   /* Always report CRC error if receiver isn't checking. */

            /* Now we have to update the FIFO. Since only 8 bytes are visible
             * in the FIFO after a receive, we can skip most of it.
             */
            for (ofs = 0; ofs < pktlen; ++ofs)
                pCore->fifo.fifo[pCore->fifo.wp++ & 7] = pktbuf[ofs];

            /* Stuff the generated FCS in the FIFO. */
            for (ofs = 0; ofs < sizeof(abFcs); ++ofs)
                pCore->fifo.fifo[pCore->fifo.wp++ & 7] = abFcs[ofs];
        }

        /* And now put the packet length in the FIFO. */
        if (fAddrMatched || 1)
        {
            pCore->fifo.fifo[pCore->fifo.wp++ & 7] = RT_LOBYTE(pktlen);
            pCore->fifo.fifo[pCore->fifo.wp++ & 7] = RT_HIBYTE(pktlen);
            pCore->fifo.fifo[pCore->fifo.wp++ & 7] = RT_HIBYTE(pktlen); /* Yes, written twice! */
        }

        Log(("FIFO: rp=%u, wp=%u\n", pCore->fifo.rp & 7, pCore->fifo.wp & 7));
        Log(("  %Rhxs\n", &pCore->fifo.fifo));

        if (nRsr.CRC)
            pCore->isr.RXE = 1;
        pCore->RSR = nRSR;

        pThis->Led.Actual.s.fReading = 0;

        /* Return success so that caller sets TSR.PTX and ISR.PTX. */
        rc = VINF_SUCCESS;
    }
    return rc;
}


/**
 * Reads the entire frame into the scatter gather buffer.
 */
DECLINLINE(void) dp8390CoreXmitRead(PPDMDEVINS pDevIns, const unsigned uLocalAddr, const unsigned cbFrame, PPDMSCATTERGATHER pSgBuf, bool fLoopback)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    unsigned        uOfs = 0;
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    Assert(pSgBuf->cbAvailable >= cbFrame);

    pSgBuf->cbUsed = cbFrame;

    LogFlowFunc(("#%d: uLocalAddr=%04X cbFrame=%d\n", pThis->iInstance, uLocalAddr, cbFrame));
    /* Have to figure out where the address is in local RAM. */
    if (pThis->uDevType == DEV_NE1000)
    {
        /* Only 14 bits of address are decoded. */
        uOfs = uLocalAddr & 0x3fff;
        if (uOfs >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            uOfs -= 0x2000;
        }
        else
        {
            /// @todo What are we supposed to do?!
            LogFunc(("#%d: uOfs=%u, don't know what to do!!\n", pThis->iInstance, uOfs));
        }
    }
    else if (pThis->uDevType == DEV_NE2000)
    {
        /* Only 15 bits of address are decoded. */
        uOfs = uLocalAddr & 0x7fff;
        if (uOfs >= 0x4000)
        {
            /* Local RAM is mapped at 4000h-7FFFh. */
            uOfs -= 0x4000;
        }
        else
        {
            /// @todo What are we supposed to do?!
            LogFunc(("#%d: uOfs=%u, don't know what to do!!\n", pThis->iInstance, uOfs));
        }
    }
    else if ((pThis->uDevType == DEV_WD8003) || (pThis->uDevType == DEV_WD8013))
    {
        /* Not much to do, WD was nice enough to put the RAM at the start of DP8390's address space. */
        uOfs = uLocalAddr & DPNIC_MEM_MASK;
    }
    else if (pThis->uDevType == DEV_3C503)
    {
        /* Only 14 bits of address are decoded. */
        uOfs = uLocalAddr & 0x3fff;
        if (uOfs >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            uOfs -= 0x2000;
        }
        else
        {
            /// @todo What are we supposed to do?!
            LogFunc(("#%d: uOfs=%u, don't know what to do!!\n", pThis->iInstance, uOfs));
        }
    }
    else
    {
        Assert(0);
    }

    if (!fLoopback)
    {
        /* Fast path for normal transmit, ignores DCR.WTS. */
        if (uOfs + cbFrame <= sizeof(pThis->abLocalRAM))
            memcpy(pSgBuf->aSegs[0].pvSeg, &pThis->abLocalRAM[uOfs], cbFrame);
        else
            memset(pSgBuf->aSegs[0].pvSeg, 0xEE, cbFrame);
    }
    else
    {
        /* If DCR.WTS is set, only every other byte actually goes through loopback. */
        const uint8_t   *src  = &pThis->abLocalRAM[uOfs];
        uint8_t         *dst  = (uint8_t *)pSgBuf->aSegs[0].pvSeg;
        int             cbDst = cbFrame;
        int             step  = 1 << pThis->core.dcr.WTS;

        /* Depending on DCR.BOS, take either odd or even bytes when DCR.WTS is set. */
        if (pThis->core.dcr.WTS && !pThis->core.dcr.BOS)
            ++src;

        while (cbDst-- && (src <= &pThis->abLocalRAM[DPNIC_MEM_SIZE]))
        {
            *dst++ = *src;
            src   += step;
        }

        /* The address should perhaps wrap around -- depends on card design. */
        if (cbDst != -1)
        {
            while (cbDst--)
                *dst++ = 0xEE;
        }
        Assert(cbDst == -1);
    }
}

/**
 * Try to transmit a frame.
 */
static void dp8390CoreStartTransmit(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    /*
     * Transmit the packet if possible, defer it if we cannot do it
     * in the current context.
     */
    pThis->core.TSR = 0;    /* Clear transmit status. */
    pThis->core.NCR = 0;    /* Clear collision counter. */
#if defined(IN_RING0) || defined(IN_RC)
    PDPNICSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDPNICSTATECC);
    if (!pThisCC->pDrv)
    {
        int rc = PDMDevHlpTaskTrigger(pDevIns, pThis->hXmitTask);
        AssertRC(rc);
    }
    else
#endif
    {
        int rc = dp8390CoreXmitPacket(pDevIns, pThis, false /*fOnWorkerThread*/);
        if (rc == VERR_TRY_AGAIN)
            rc = VINF_SUCCESS;
        AssertRC(rc);
    }
}


/**
 * If a packet is waiting, poke the receiving machinery.
 *
 * @threads EMT.
 */
static void dp8390CoreKickReceive(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    if (pThis->fMaybeOutOfSpace)
    {
        LogFlow(("Poking receive thread.\n"));
#ifdef IN_RING3
        dp8390R3WakeupReceive(pDevIns);
#else
        int rc = PDMDevHlpTaskTrigger(pDevIns, pThis->hCanRxTask);
        AssertRC(rc);
#endif
    }
}

/**
 * Try transmitting a frame.
 *
 * @threads TX or EMT.
 */
static int dp8390CoreAsyncXmitLocked(PPDMDEVINS pDevIns, PDPNICSTATE pThis, PDPNICSTATECC pThisCC, bool fOnWorkerThread)
{
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));

    /*
     * Just drop it if not transmitting. Can happen with delayed transmits
     * if transmit was disabled in the meantime.
     */
    if (RT_UNLIKELY(!pThis->core.cr.TXP))
    {
        LogFunc(("#%d: Nope, CR.TXP is off (fOnWorkerThread=%RTbool)\n", pThis->iInstance, fOnWorkerThread));
        return VINF_SUCCESS;
    }

    /*
     * Blast out data from the packet buffer.
     */
    int         rc;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatTransmit), a);
    do
    {
        /* Don't send anything when the link is down. */
        if (RT_UNLIKELY(   !dp8390IsLinkUp(pThis)
                        &&  pThis->cLinkDownReported > DPNIC_MAX_LINKDOWN_REPORTED)
            )
            break;

        bool const          fLoopback = pThis->core.tcr.LB != 0;
        PDMSCATTERGATHER    SgLoop;
        PPDMSCATTERGATHER   pSgBuf;

        /*
         * Sending is easy peasy, there is by definition always
         * a complete packet on hand.
         */
        unsigned    cb  = pThis->core.TBCR; /* Packet size. */
        const int   adr = RT_MAKE_U16(0, pThis->core.TPSR);
        LogFunc(("#%d: cb=%d, adr=%04X\n", pThis->iInstance, cb, adr));

        if (RT_LIKELY(dp8390IsLinkUp(pThis) || fLoopback))
        {
            if (RT_LIKELY(cb <= MAX_FRAME))
            {
                /* Loopback fun! */
                if (RT_UNLIKELY(fLoopback && pThis->core.dcr.WTS))
                {
                    cb /= 2;
                    Log(("Loopback with DCR.WTS set -> cb=%d\n", cb));
                }

                rc = dp8390XmitAllocBuf(pThis, pThisCC, cb, fLoopback, &SgLoop, &pSgBuf);
                if (RT_SUCCESS(rc))
                {
                    dp8390CoreXmitRead(pDevIns, adr, cb, pSgBuf, fLoopback);
                    rc = dp8390CoreXmitSendBuf(pDevIns, pThisCC, fLoopback, pSgBuf, fOnWorkerThread);
                    Log2Func(("#%d: rc=%Rrc\n", pThis->iInstance, rc));
                }
                else if (rc == VERR_TRY_AGAIN)
                {
                    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
                    LogFunc(("#%d: rc=%Rrc\n", pThis->iInstance, rc));
                    return VINF_SUCCESS;
                }
                if (RT_SUCCESS(rc))
                {
                    pThis->core.tsr.PTX = 1;
                    pThis->core.isr.PTX = 1;
                }
                else
                {
                    pThis->core.tsr.COL = 1;    /* Pretend there was a collision. */
                    pThis->core.isr.TXE = 1;
                }
            }
            else
            {
                /* Signal error, as this violates the Ethernet specs. Note that the DP8390
                 * hardware does *not* limit the packet length.
                 */
                LogRel(("DPNIC#%d: Attempt to transmit illegal giant frame (%u bytes) -> signaling error\n", pThis->iInstance, cb));
                pThis->core.tsr.OWC = 1;    /* Pretend there was an out-of-window collision. */
                pThis->core.isr.TXE = 1;
            }
        }
        else
        {
            /* Signal a transmit error pretending there was a collision. */
            pThis->core.tsr.COL = 1;
            pThis->core.isr.TXE = 1;
            pThis->cLinkDownReported++;
        }
        /* Transmit officially done, update register state. */
        pThis->core.cr.TXP = 0;
        pThis->core.TBCR   = 0;
        LogFlowFunc(("#%d: TSR=%02X, ISR=%02X\n", pThis->iInstance, pThis->core.TSR, pThis->core.ISR));

    } while (0);    /* No loop, because there isn't ever more than one packet to transmit. */

    dp8390CoreUpdateIrq(pDevIns, pThis);

    /* If there's anything waiting, this should be a good time to recheck. */
    dp8390CoreKickReceive(pDevIns, pThis);

    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);

    return VINF_SUCCESS;
}

/* -=-=-=-=-=- I/O Port access -=-=-=-=-=- */


static uint32_t dp8390CoreRead(PPDMDEVINS pDevIns, PDPNICSTATE pThis, int ofs)
{
    uint8_t     val;

    /* The 3C503 can read the PROM instead of the DP8390 registers. */
    if (pThis->ga.gacr.ealo)
        return pThis->aPROM[ofs % 0xf];
    else if (pThis->ga.gacr.eahi)
        return pThis->aPROM[16 + (ofs % 0xf)];

    /* Command Register exists in all pages. */
    if (ofs == DPR_CR)
        return pThis->core.CR;

    if (pThis->core.cr.PS == 0)
    {
        switch (ofs)
        {
        case DPR_P0_R_CLDA0:
            return pThis->core.clda.CLDA0;
        case DPR_P0_R_CLDA1:
            return pThis->core.clda.CLDA1;
        case DPR_P0_BNRY:
            return pThis->core.BNRY;
        case DPR_P0_R_TSR:
            return pThis->core.TSR;
        case DPR_P0_R_NCR:
            return pThis->core.NCR;
        case DPR_P0_R_FIFO:
            return pThis->core.fifo.fifo[pThis->core.fifo.rp++ & 7];    /// @todo Abstract the mask somehow?
        case DPR_P0_ISR:
            return pThis->core.ISR;
        case DPR_P0_R_CRDA0:
            return pThis->core.crda.CRDA0;
        case DPR_P0_R_CRDA1:
            return pThis->core.crda.CRDA1;
        case DPR_P0_R_RSR:
            return pThis->core.RSR;
        case DPR_P0_R_CNTR0:
            val = pThis->core.CNTR0;
            pThis->core.CNTR0 = 0;  /* Cleared by reading. */
            dp8390CoreUpdateIrq(pDevIns, pThis);
            return val;
        case DPR_P0_R_CNTR1:
            val = pThis->core.CNTR1;
            pThis->core.CNTR1 = 0;  /* Cleared by reading. */
            dp8390CoreUpdateIrq(pDevIns, pThis);
            return val;
        case DPR_P0_R_CNTR2:
            val = pThis->core.CNTR2;
            pThis->core.CNTR2 = 0;  /* Cleared by reading. */
            dp8390CoreUpdateIrq(pDevIns, pThis);
            return val;
        default:
            return 0;   /// @todo or 0xFF? or something else?
        }
    }
    else if (pThis->core.cr.PS == 1)
    {
        /* Page 1 is easy, most registers are stored directly. */
        if (ofs == DPR_P1_CURR)
            return pThis->core.CURR;
        else
            return pThis->core.PG1[ofs];
    }
    else if (pThis->core.cr.PS == 2)
    {
        /* Page 2 is for diagnostics. Reads many registers that
         * are write-only in Page 0.
         */
        switch (ofs)
        {
        case DPR_P2_R_PSTART:
            return pThis->core.PSTART;
        case DPR_P2_R_PSTOP:
            return pThis->core.PSTOP;
        case DPR_P2_RNXTPP:
            return pThis->core.rnxtpp;
        case DPR_P2_R_TPSR:
            return pThis->core.TPSR;
        case DPR_P2_LNXTPP:
            return pThis->core.lnxtpp;
        case DPR_P2_ADRCU:
        case DPR_P2_ADRCL:
            return 0;   /// @todo What's this?
        case DPR_P2_R_RCR:
            return pThis->core.RCR;
        case DPR_P2_R_TCR:
            return pThis->core.TCR;
        case DPR_P2_R_DCR:
            return pThis->core.DCR;
        case DPR_P2_R_IMR:
            return pThis->core.IMR;
        default:
            return 0;   /// @todo Or 0xFF? Or something else?
        }
    }
    else
    {
        /* Page 3 is undocumented and unimplemented. */
        LogFunc(("Reading page 3 register: ofs=%X!\n", ofs));
        return 0;
    }
}


static int dp8390CoreWriteCR(PPDMDEVINS pDevIns, PDPNICSTATE pThis, uint32_t val)
{
    union {
        uint8_t     nCR;
        DP_CR       nCr;
    };

    nCR = val;
    LogFlow(("val=%02X, old=%02X\n", val, pThis->core.CR));
    if (nCr.STP != pThis->core.cr.STP)
    {
        if (nCr.STP)
        {
            /* Stop the engine -- software reset. */
            pThis->core.cr.STP  = 1;
            pThis->core.isr.RST = 1;
        }
        else
        {
            /* Clear the stop condition. */
            pThis->core.cr.STP  = 0;

            /* And possibly start up right away. */
            if (nCr.STA)
                pThis->core.cr.STA  = 1;

            /* The STA bit may have been set all along. */
            if (pThis->core.cr.STA)
                pThis->core.isr.RST = 0;
        }

        /* Unblock receive thread if necessary, possibly drop any packets. */
        dp8390CoreKickReceive(pDevIns, pThis);
    }
    if (nCr.STA && !pThis->core.cr.STA)
    {
        /* Start the engine. It is not clearly documented but the STA bit is
         * sticky, and once it's set only a hard reset can clear it. Setting the
         * STP bit doesn't clear it.
         */
        pThis->core.cr.STA  = 1;
        pThis->core.isr.RST = 0;

        /* Unblock receive thread. */
        dp8390CoreKickReceive(pDevIns, pThis);
    }
    if (nCr.TXP && !pThis->core.cr.TXP)
    {
        /* Kick off a transmit. */
        pThis->core.cr.TXP = 1;     /* Indicate transmit in progress. */
        dp8390CoreStartTransmit(pDevIns, pThis);
    }

    /* It is not possible to write a zero (invalid value) to the RD bits. */
    if (nCr.RD == DP_CR_RDMA_INVL)
        nCr.RD = DP_CR_RDMA_ABRT;

    if (nCr.RD != pThis->core.cr.RD)
    {
        /* Remote DMA state change. */
        if (nCr.RD & DP_CR_RDMA_ABRT)
        {
            /* Abort. */
            LogFunc(("RDMA Abort! RD=%d RSAR=%04X RBCR=%04X CRDA=%04X\n", nCr.RD, pThis->core.RSAR, pThis->core.RBCR, pThis->core.CRDA));
        }
        else if (nCr.RD == DP_CR_RDMA_SP)
        {
            DP_PKT_HDR  header;

            /* Read a packet header from memory at BNRY. */
            dpLocalRAMReadBuf(pThis, pThis->core.BNRY, sizeof(header), (uint8_t*)&header);

            pThis->core.CRDA = RT_MAKE_U16(0, pThis->core.BNRY);
            pThis->core.RBCR = header.byte_cnt;

            LogFunc(("RDMA SP: RD=%d RSAR=%04X RBCR=%04X CRDA=%04X\n", nCr.RD, pThis->core.RSAR, pThis->core.RBCR, pThis->core.CRDA));
        }
        else
        {
            /* Starting remote DMA read or write. */
            LogFunc(("RDMA: RD=%d RSAR=%04X RBCR=%04X\n", nCr.RD, pThis->core.RSAR, pThis->core.RBCR));
        }
        pThis->core.cr.RD = nCr.RD;
        /* NB: The current DMA address (CRDA) is not modified here. */
    }
    /* Set the page select bits. */
    pThis->core.cr.PS = nCr.PS;

    return VINF_SUCCESS;
}

static int dp8390CoreWrite(PPDMDEVINS pDevIns, PDPNICSTATE pThis, int ofs, uint32_t val)
{
    int     rc = VINF_SUCCESS;
    bool    fUpdateIRQ = false;

    Log2Func(("#%d: page=%d reg=%X val=%02X\n", pThis->iInstance, pThis->core.cr.PS, ofs, val));

    /* Command Register exists in all pages. */
    if (ofs == DPR_CR)
    {
        rc = dp8390CoreWriteCR(pDevIns, pThis, val);
    }
    else if (pThis->core.cr.PS == 0)
    {
        switch (ofs)
        {
        case DPR_P0_W_PSTART:
            pThis->core.PSTART = val;
            pThis->core.CURR   = val;
            break;
        case DPR_P0_W_PSTOP:
            pThis->core.PSTOP = val;
            break;
        case DPR_P0_BNRY:
            if (pThis->core.BNRY != val)
            {
                pThis->core.BNRY = val;
                /* Probably made more room in receive ring. */
                dp8390CoreKickReceive(pDevIns, pThis);
            }
            break;
        case DPR_P0_W_TPSR:
            pThis->core.TPSR = val;
            break;
        case DPR_P0_W_TBCR0:
            pThis->core.tbcr.TBCR0 = val;
            break;
        case DPR_P0_W_TBCR1:
            pThis->core.tbcr.TBCR1 = val;
            break;
        case DPR_P0_ISR:
            /* Bits are cleared by writing 1 to them, except for bit 7 (RST). */
            pThis->core.ISR &= ~val | RT_BIT(7);
            fUpdateIRQ = true;
            break;
        case DPR_P0_W_RSAR0:
            /* NE2000 ODI driver v2.12 detects card presence by writing RSAR0
             * and checking if CRDA0 changes to the same value.
             */
            pThis->core.rsar.RSAR0 = val;
            pThis->core.crda.CRDA0 = val;
            break;
        case DPR_P0_W_RSAR1:
            pThis->core.rsar.RSAR1 = val;
            pThis->core.crda.CRDA1 = val;
            break;
        case DPR_P0_W_RBCR0:
            pThis->core.rbcr.RBCR0 = val;
            break;
        case DPR_P0_W_RBCR1:
            pThis->core.rbcr.RBCR1 = val;
            break;
        case DPR_P0_W_RCR:
            pThis->core.RCR     = val;
            pThis->core.rsr.DIS = pThis->core.rcr.MON;
            break;
        case DPR_P0_W_TCR:
            pThis->core.TCR = val;
            break;
        case DPR_P0_W_DCR:
            pThis->core.DCR = val;
            break;
        case DPR_P0_W_IMR:
            pThis->core.IMR = val & 0x7f;   /* Don't let the high bit get set. */
            fUpdateIRQ = true;
            break;
        default:
            Assert(0);
            break;
        }
    }
    else if (pThis->core.cr.PS == 1)
    {
        /* Page 1 is easy, most registers are stored directly. */
        if (ofs == DPR_P1_CURR)
        {
            pThis->core.CURR = val;
        }
        else
            pThis->core.PG1[ofs] = val;
    }
    else if (pThis->core.cr.PS == 2)
    {
        switch (ofs)
        {
        case DPR_P2_W_CLDA0:
            pThis->core.clda.CLDA0 = val;
            break;
        case DPR_P2_W_CLDA1:
            pThis->core.clda.CLDA1 = val;
            break;
        case DPR_P2_RNXTPP:
            pThis->core.rnxtpp = val;
            break;
        case DPR_P2_LNXTPP:
            pThis->core.lnxtpp = val;
            break;
        case DPR_P2_ADRCU:
        case DPR_P2_ADRCL:
            /// @todo What are these?
            break;
        default:
            LogFunc(("Writing unimplemented register: Page 2, offset=%d, val=%02X!\n", ofs, val));
            break;
        }
    }
    else
    {
        /* Page 3 is undocumented and unimplemented. */
        LogFunc(("Writing page 3 register: offset=%d, val=%02X!\n", ofs, val));
    }

    if (fUpdateIRQ)
        dp8390CoreUpdateIrq(pDevIns, pThis);

    return rc;
}


static void neLocalRAMWrite8(PDPNICSTATE pThis, uint16_t addr, uint8_t val)
{
    if (pThis->uDevType == DEV_NE1000)
    {
        /* Only 14 bits of address are decoded. */
        addr &= 0x3fff;
        if (addr >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            addr -= 0x2000;
            pThis->abLocalRAM[addr] = val;
        }
    }
    else if (pThis->uDevType == DEV_NE2000)
    {
        /* Only 15 bits of address are decoded. */
        addr &= 0x7fff;
        if (addr >= 0x4000)
        {
            /* Local RAM is mapped at 4000h-7FFFh. */
            addr -= 0x4000;
            pThis->abLocalRAM[addr] = val;
        }
    }
    else
    {
        Assert(0);
    }
}


static void neLocalRAMWrite16(PDPNICSTATE pThis, uint16_t addr, uint16_t val)
{
    if (pThis->uDevType == DEV_NE2000)
    {
        /* Only 14 bits of address are decoded, word aligned. */
        addr &= 0x7ffe;
        if (addr >= 0x4000)
        {
            /* Local RAM is mapped at 4000h-7FFFh. */
            addr -= 0x4000;
            pThis->abLocalRAM[addr+0] = RT_LOBYTE(val);
            pThis->abLocalRAM[addr+1] = RT_HIBYTE(val);
        }
    }
    else
    {
        Assert(0);
    }
}


static uint8_t neLocalRAMRead8(PDPNICSTATE pThis, uint16_t addr)
{
    uint8_t     val = 0xff;

    if (pThis->uDevType == DEV_NE1000)
    {
        /* Only 14 bits of address are decoded. */
        addr &= 0x3fff;
        if (addr >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            addr -= 0x2000;
            val = pThis->abLocalRAM[addr];
        }
        else
        {
            /* The PROM is mapped below 2000h, effectively only 4 bits decoded.
             * NE1000 emulation uses top 16 bytes of the PROM.
             */
            val = pThis->aPROM[(addr & 0x0f) + 16]; /// @todo Use a constant
        }
    }
    else if (pThis->uDevType == DEV_NE2000)
    {
        /* Only 15 bits of address are decoded. */
        addr &= 0x7fff;
        if (addr >= 0x4000)
        {
            /* Local RAM is mapped at 4000h-7FFFh. */
            addr -= 0x4000;
            val = pThis->abLocalRAM[addr];
        }
        else
        {
            /* The PROM is mapped below 4000h, effectively only 4 bits decoded.
             * Address bits 1:4 from the bus are connected to address pins 0:3
             * on the PROM.
             */
            val = pThis->aPROM[(addr & 0x1f) >> 1]; /// @todo use a constant
        }
    }
    else
    {
        Assert(0);
    }
    return val;
}


static uint16_t neLocalRAMRead16(PDPNICSTATE pThis, uint16_t addr)
{
    uint16_t    val = 0xffff;

    if (pThis->uDevType == DEV_NE2000)
    {
        /* Only 14 bits of address are decoded, word aligned. */
        addr &= 0x7ffe;
        if (addr >= 0x4000)
        {
            /* Local RAM is mapped at 4000h-7FFFh. */
            addr -= 0x4000;
            val = RT_MAKE_U16(pThis->abLocalRAM[addr], pThis->abLocalRAM[addr+1]);
        }
        else
        {
            uint8_t     uPromByte;

            /* The PROM is mapped below 4000h, effectively only 4 bits decoded.
             * Address bits 1:4 from the bus are connected to address pins 0:3
             * on the PROM.
             */
            uPromByte = pThis->aPROM[(addr & 0x1f) >> 1];
            val = RT_MAKE_U16(uPromByte, uPromByte);
        }
    }
    else
    {
        Assert(0);
    }
    return val;
}


static int neDataPortWrite(PPDMDEVINS pDevIns, PDPNICSTATE pThis, uint16_t val)
{
    /* Remote Write; ignored if Remote DMA command is not 'Write'. */
    if (pThis->core.cr.RD == DP_CR_RDMA_WR)
    {
        /// @todo Also do nothing if DCR.LAS set?
        if (pThis->core.dcr.WTS)
        {
            Log3Func(("RDMA16 write %04X to local addr %04X\n", val, pThis->core.CRDA));
            neLocalRAMWrite16(pThis, pThis->core.CRDA, val);
        }
        else
        {
            Log3Func(("RDMA8 write %02X to local addr %04X\n", val, pThis->core.CRDA));
            neLocalRAMWrite8(pThis, pThis->core.CRDA, val);
        }
        pThis->core.CRDA += 1 << pThis->core.dcr.WTS;
        if ((pThis->core.crda.CRDA1 == pThis->core.PSTOP) && (pThis->core.PSTOP != pThis->core.PSTART))
        {
            LogFunc(("RDMA wrap / write!! (CRDA=%04X PSTOP=%02X00 PSTART=%02X00)\n", pThis->core.CRDA, pThis->core.PSTOP, pThis->core.PSTART));
            Assert(!pThis->core.crda.CRDA0);    /// @todo Can misalignment actually happen?
            pThis->core.crda.CRDA1 = pThis->core.PSTART;
        }
        pThis->core.RBCR -= 1;

        /* Carefully decrement if WTS set so we don't overshoot and miss EOP. */
        if (pThis->core.dcr.WTS && pThis->core.RBCR)
            pThis->core.RBCR -= 1;

        if (!pThis->core.RBCR)
        {
            LogFunc(("RDMA EOP / write\n"));
            pThis->core.isr.RDC = 1;
            pThis->core.cr.RD   = 0;
            dp8390CoreUpdateIrq(pDevIns, pThis);
        }
    }
    return VINF_SUCCESS;
}


static uint16_t neDataPortRead(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    uint16_t    val = 0x1234;

    /* Remote Read; ignored if Remote DMA command is not 'Read'. */
    if (pThis->core.cr.RD == DP_CR_RDMA_RD)
    {
        /// @todo Also do nothing if DCR.LAS set?
        if (pThis->core.dcr.WTS)
        {
            val = neLocalRAMRead16(pThis, pThis->core.CRDA);
            Log3Func(("RDMA16 read from local addr %04X: %04X\n", pThis->core.CRDA, val));
        }
        else
        {
            val = neLocalRAMRead8(pThis, pThis->core.CRDA);
            Log3Func(("RDMA8 read from local addr %04X: %02X\n", pThis->core.CRDA, val));
        }
        pThis->core.CRDA += 1 << pThis->core.dcr.WTS;
        /// @todo explain that PSTOP=PSTART check is only to reduce logging/busywork
        if ((pThis->core.crda.CRDA1 == pThis->core.PSTOP) && (pThis->core.PSTOP != pThis->core.PSTART))
        {
            Log3Func(("RDMA wrap / read (CRDA=%04X PSTOP=%02X00 PSTART=%02X00)\n", pThis->core.CRDA, pThis->core.PSTOP, pThis->core.PSTART));
            Assert(!pThis->core.crda.CRDA0);    /// @todo can misalignment happen?
            pThis->core.crda.CRDA1 = pThis->core.PSTART;
        }
        pThis->core.RBCR -= 1;

        /* Carefully decrement if WTS set so we don't overshoot and miss EOP. */
        if (pThis->core.dcr.WTS && pThis->core.RBCR)
            pThis->core.RBCR -= 1;

        if (!pThis->core.RBCR)
        {
            LogFunc(("RDMA EOP / read\n"));
            pThis->core.isr.RDC = 1;
            pThis->core.cr.RD   = 0;
            dp8390CoreUpdateIrq(pDevIns, pThis);
        }
    }
    return val;
}


static int neResetPortWrite(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    LogFlowFunc(("\n"));
    dp8390CoreReset(pDevIns, pThis);
    return VINF_SUCCESS;
}


static int dpNeIoWrite(PPDMDEVINS pDevIns, PDPNICSTATE pThis, uint32_t addr, uint32_t val)
{
    int     reg = addr & 0x0f;
    int     rc = VINF_SUCCESS;

    Log2Func(("#%d: addr=%#06x val=%#04x\n", pThis->iInstance, addr, val & 0xff));

    /* The NE2000 has 8 bytes of data port followed by 8 bytes of reset port.
     * In contrast, the NE1000 has 4 bytes of data port followed by 4 bytes
     * of reset port, aliased twice within the 16-byte range.
     */
    if (pThis->uDevType == DEV_NE2000)
        reg >>= 1;
    if (reg & 0x04)
        rc = neResetPortWrite(pDevIns, pThis);
    else
        rc = neDataPortWrite(pDevIns, pThis, val);

    return rc;
}


static uint32_t neIoRead(PPDMDEVINS pDevIns, PDPNICSTATE pThis, uint32_t addr)
{
    uint32_t    val = UINT32_MAX;
    int         reg = addr & 0x0f;

    /* The NE2000 has 8 bytes of data port followed by 8 bytes of reset port.
     * In contrast, the NE1000 has 4 bytes of data port followed by 4 bytes
     * of reset port, aliased twice within the 16-byte range.
     */
    if (pThis->uDevType == DEV_NE2000)
        reg >>= 1;
    if (reg & 0x04)
        val = 0x52; /// @todo Check what really happens
    else
        val = neDataPortRead(pDevIns, pThis);

    Log2Func(("#%d: addr=%#06x val=%#04x\n", pThis->iInstance, addr, val & 0xff));
    return val;
}


static int wdIoWrite(PPDMDEVINS pDevIns, PDPNICSTATE pThis, uint32_t addr, uint32_t val)
{
    int             reg = addr & 0xf;
    int             rc = VINF_SUCCESS;
    union {
        uint8_t     nCTRL1;
        WD_CTRL1    nCtrl1;
    };
    union {
        uint8_t     nCTRL2;
        WD_CTRL2    nCtrl2;
    };

    Log2Func(("#%d: addr=%#06x val=%#04x\n", pThis->iInstance, addr, val & 0xff));

    switch (reg)
    {
    case WDR_CTRL1:
        nCTRL1 = val;
        if (nCtrl1.MEME != pThis->ctrl1.MEME)
        {
            LogFunc(("CTRL1.MEME=%u\n", nCtrl1.MEME));
            pThis->ctrl1.MEME = nCtrl1.MEME;
        }
        if (nCtrl1.RESET)
        {
            dp8390CoreReset(pDevIns, pThis);
            pThis->CTRL1 = 0;
        }
        break;
    case WDR_CTRL2:
        /* NYI. */
        nCTRL2 = val;
        if (nCTRL2 != pThis->CTRL2)
        {
            LogFunc(("CTRL2=%02X, new=%02X\n", pThis->CTRL2, nCTRL2));
            pThis->CTRL2 = nCTRL2;
        }
        break;
    default:
        /* Most of the WD registers are read-only. */
        break;
    }

    return rc;
}


static uint32_t wdIoRead(PDPNICSTATE pThis, uint32_t addr)
{
    uint32_t    val = UINT32_MAX;
    int         reg = addr & 0x0f;

    if (reg >= WDR_PROM)
    {
        val = pThis->aPROM[reg & 7];
    }
    else
    {
        if (pThis->uDevType == DEV_WD8013)
        {
            switch (reg)
            {
            case WDR_CTRL1:
                val = pThis->CTRL1;
                break;
            case WDR_ATDET:
                val = pThis->uDevType == DEV_WD8013 ? 1 : 0;
                break;
            case WDR_IOBASE:
                val = pThis->aPROM[WDR_IOBASE]; //val = pThis->IOPortBase >> 5;
                break;
            case WDR_CTRL2:
                val = pThis->CTRL2;
                break;
            case WDR_JP:
                val = 0xa0;
                break;
            default:
                val = 0x00; /// @todo What should it be really?
                break;
            }
        }
        else
        {
            /* Old WD adapters (including 8003E) aliased the PROM for
             * unimplemented control register reads.
             */
            switch (reg)
            {
            case WDR_CTRL2:
                val = 1; //pThis->CTRL2;
                break;
            case WDR_JP:
                val = 0xa0;
                break;
            default:
                val = pThis->aPROM[reg & 7];
                break;
            }
        }

    }

    Log2Func(("#%d: addr=%#06x val=%#04x\n", pThis->iInstance, addr, val & 0xff));
    return val;
}


static uint8_t elGetIrqFromIdcfr(uint8_t val)
{
    union {
        uint8_t     IDCFR;
        EL_IDCFR    idcfr;
    };
    uint8_t         irq = 0;

    IDCFR = val;

    /* Lowest set IRQ bit wins (might not match hardware).
     * NB: It is valid to not enable any IRQ line!
     */
    if (idcfr.irq2)
        irq = 2;
    else if (idcfr.irq3)
        irq = 3;
    else if (idcfr.irq4)
        irq = 4;
    else if (idcfr.irq5)
        irq = 5;

    return irq;
}

static uint8_t elGetDrqFromIdcfr(uint8_t val)
{
    union {
        uint8_t     IDCFR;
        EL_IDCFR    idcfr;
    };
    uint8_t         drq = 0;

    IDCFR = val;

    /* Lowest set DRQ bit wins; it is valid to not set any. */
    if (idcfr.drq1)
        drq = 1;
    else if (idcfr.drq2)
        drq = 2;
    else if (idcfr.drq3)
        drq = 3;

    return drq;
}

static void elWriteIdcfr(PPDMDEVINS pDevIns, PDPNICSTATE pThis, PEL_GA pGa, uint8_t val)
{
    uint8_t     uOldIrq = pThis->uIsaIrq;
    uint8_t     uNewIrq;
    uint8_t     uOldDrq = pThis->uElIsaDma;
    uint8_t     uNewDrq;

    /* If the IRQ is currently active, have to switch it. */
    uNewIrq = elGetIrqFromIdcfr(val);
    if (uOldIrq != uNewIrq)
    {
        LogFunc(("#%d Switching IRQ=%d -> IRQ=%d\n", pThis->iInstance, uOldIrq, uNewIrq));
        if (pThis->fNicIrqActive)
        {
            /* This probably isn't supposed to happen. */
            LogFunc(("#%d Moving active IRQ!\n", pThis->iInstance));
            if (uOldIrq)
                PDMDevHlpISASetIrq(pDevIns, uOldIrq, 0);
            if (uNewIrq)
                PDMDevHlpISASetIrq(pDevIns, uNewIrq, 1);
        }
        pThis->uIsaIrq = uNewIrq;
    }

    /* And now the same dance for DMA. */
    uNewDrq = elGetDrqFromIdcfr(val);
    if (uOldDrq != uNewDrq)
    {
        /// @todo We can't really move the DRQ, what can we do?
        LogFunc(("#%d Switching DRQ=%d -> DRQ=%d\n", pThis->iInstance, uOldDrq, uNewDrq));
        pThis->uElIsaDma = uNewDrq;
    }

    pGa->IDCFR = val;
}


static void elWriteGacfr(PPDMDEVINS pDevIns, PDPNICSTATE pThis, PEL_GA pGa, uint8_t val)
{
    union {
        uint8_t     nGACFR;
        GA_GACFR    nGacfr;
    };

    nGACFR = val;

    if (nGacfr.nim != pGa->gacfr.nim)
    {
        /// @todo Should we just run UpdateInterrupts?
        if (pThis->fNicIrqActive && !nGacfr.nim)
        {
            LogFunc(("#%d: Unmasking active IRQ!\n", pThis->iInstance));
            PDMDevHlpISASetIrq(pDevIns, pThis->uIsaIrq, 1);
        }
        else if (pThis->fNicIrqActive && nGacfr.nim)
        {
            LogFunc(("#%d: Masking active IRQ\n", pThis->iInstance));
            PDMDevHlpISASetIrq(pDevIns, pThis->uIsaIrq, 0);
        }
    }

    /// @todo rsel/mbs bit change?
    if (nGacfr.rsel != pGa->gacfr.rsel)
    {
        LogFunc(("#%d: rsel=%u mbs=%u\n", pThis->iInstance, nGacfr.rsel, nGacfr.mbs));
    }

    pGa->GACFR = nGACFR;
}


static void elSoftReset(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    PEL_GA      pGa = &pThis->ga;

    LogFlow(("Resetting ASIC GA\n"));
    /* Most GA registers are zeroed. */
    pGa->PSTR = pGa->PSPR = 0;
    pGa->DQTR = 0;
    elWriteGacfr(pDevIns, pThis, pGa, 0);
    pGa->STREG = ELNKII_GA_REV;
    pGa->VPTR0 = pGa->VPTR1 = pGa->VPTR2 = 0;
    pGa->DALSB = pGa->DAMSB = 0;
    elWriteIdcfr(pDevIns, pThis, pGa, 0);
    pGa->GACR = 0x0B;   /* Low bit set = in reset state. */
    pGa->fGaIrq = false;

    /* Reset the NIC core. */
    dp8390CoreReset(pDevIns, pThis);
}


static int elWriteGacr(PPDMDEVINS pDevIns, PDPNICSTATE pThis, PEL_GA pGa, uint8_t val)
{
    union {
        uint8_t     nGACR;
        GA_GACR     nGacr;
    };

    nGACR = val;

    if (nGacr.rst != pGa->gacr.rst)
    {
        /* When going out of reset, only clear the rst bit. 3C503 diagnostics checks for this. */
        if (nGacr.rst)
            elSoftReset(pDevIns, pThis);
        else
            pGa->gacr.rst = 0;
    }
    else
    {
#ifdef IN_RING0
        /* Force a trip to R3. */
        if (pThis->uElIsaDma == pThis->uIsaDma)
            return VINF_IOM_R3_IOPORT_WRITE;
#endif

        /* Make the data registers "ready" as long as transfers are started. */
        if (nGacr.start)
        {
            pGa->cdadr.cdadr_lsb = pGa->DALSB;
            pGa->cdadr.cdadr_msb = pGa->DAMSB;
            LogFunc(("DMA started, ddir=%u, cdadr=%04X\n", pGa->gacr.ddir, pGa->CDADR));
            pGa->streg.dprdy = 1;
            pGa->streg.dip   = 1;
            pGa->streg.dtc   = 0;
        }
        else
        {
            pGa->streg.dprdy = 0;
            pGa->streg.dip   = 0;
        }

        /* Only do anything if the software configured DMA channel matches the emulation config. */
        if (pThis->uElIsaDma == pThis->uIsaDma)
        {
#ifdef IN_RING3
            PDMDevHlpDMASetDREQ(pDevIns, pThis->uIsaDma, pGa->streg.dprdy);
            if (pGa->streg.dprdy)
                PDMDevHlpDMASchedule(pDevIns);
            LogFunc(("#%d: DREQ for channel %u set to %u\n", pThis->iInstance, pThis->uIsaDma, pGa->streg.dprdy));
#else
            /* Must not get here. */
            Assert(0);
#endif
        }

        pGa->GACR = nGACR;
        LogFunc(("GACR=%02X ealo=%u eahi=%u\n", pGa->GACR, pGa->gacr.ealo, pGa->gacr.eahi));
    }

    return VINF_SUCCESS;
}


static int elGaDataWrite(PDPNICSTATE pThis, PEL_GA pGa, uint16_t val)
{
    /* Data write; ignored if not started and in "download" mode. */
    if (pGa->gacr.start && pGa->gacr.ddir)
    {
        uint16_t    addr = pGa->CDADR;

        addr &= 0x3fff;
        if (addr >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            addr -= 0x2000;
            pThis->abLocalRAM[addr] = val;
        }

        pGa->CDADR++;
        /// @todo Does this really apply to writes or only reads?
        if ((pGa->cdadr.cdadr_msb == pGa->PSPR) && (pGa->PSPR != pGa->PSTR))
        {
            LogFunc(("GA DMA wrap / write!! (cdadr=%04X PSPR=%02X00 PSTR=%02X00)\n", pGa->CDADR, pGa->PSPR, pGa->PSTR));
            pGa->cdadr.cdadr_msb = pGa->PSTR;
        }
    }
    return VINF_SUCCESS;
}


static uint8_t elGaDataRead(PDPNICSTATE pThis, PEL_GA pGa)
{
    uint8_t     val = 0xcd;

    /* Data read; ignored if not started and in "upload" mode. */
    if (pGa->gacr.start && !pGa->gacr.ddir)
    {
        uint16_t    addr = pGa->CDADR;

        addr &= 0x3fff;
        if (addr >= 0x2000)
        {
            /* Local RAM is mapped at 2000h-3FFFh. */
            addr -= 0x2000;
            val = pThis->abLocalRAM[addr];
        }

        pGa->CDADR++;
        if ((pGa->cdadr.cdadr_msb == pGa->PSPR) && (pGa->PSPR != pGa->PSTR))
        {
            LogFunc(("GA DMA wrap / read!! (cdadr=%04X PSPR=%02X00 PSTR=%02X00)\n", pGa->CDADR, pGa->PSPR, pGa->PSTR));
            pGa->cdadr.cdadr_msb = pGa->PSTR;
        }
    }
    return val;
}


static int elGaIoWrite(PPDMDEVINS pDevIns, PDPNICSTATE pThis, uint32_t addr, uint32_t val)
{
    int         reg = addr & 0xf;
    int         rc = VINF_SUCCESS;
    PEL_GA      pGa = &pThis->ga;

    Log2Func(("#%d: addr=%#06x val=%#04x\n", pThis->iInstance, addr, val & 0xff));

    switch (reg)
    {
    case GAR_PSTR:
        pGa->PSTR = val;
        break;
    case GAR_PSPR:
        pGa->PSPR = val;
        break;
    case GAR_DQTR:
        pGa->DQTR = val;
        break;
    case GAR_GACFR:
        elWriteGacfr(pDevIns, pThis, pGa, val);
        break;
    case GAR_GACR:
        rc = elWriteGacr(pDevIns, pThis, pGa, val);
        break;
    case GAR_STREG:
        /* Writing anything to STREG clears ASIC interrupt. */
        pThis->ga.streg.dtc = 0;
        pThis->ga.fGaIrq    = false;
        dp8390CoreUpdateIrq(pDevIns, pThis);
        break;
    case GAR_IDCFR:
        elWriteIdcfr(pDevIns, pThis, pGa, val);
        break;
    case GAR_DAMSB:
        pGa->DAMSB = val;
        break;
    case GAR_DALSB:
        pGa->DALSB = val;
        break;
    case GAR_VPTR2:
        pGa->VPTR2 = val;
        break;
    case GAR_VPTR1:
        pGa->VPTR1 = val;
        break;
    case GAR_VPTR0:
        pGa->VPTR0 = val;
        break;
    case GAR_RFMSB:
    case GAR_RFLSB:
        elGaDataWrite(pThis, pGa, val);
        break;
    case GAR_R_BCFR:
    case GAR_R_PCFR:
        /* Read-only registers, ignored. */
        break;
    default:
        Assert(0);
        break;
    }

    return rc;
}


static uint32_t elGaIoRead(PDPNICSTATE pThis, uint32_t addr)
{
    uint32_t    val = UINT32_MAX;
    int         reg = addr & 0x0f;
    PEL_GA      pGa = &pThis->ga;

    switch (reg)
    {
    case GAR_PSTR:
        val = pGa->PSTR;
        break;
    case GAR_PSPR:
        val = pGa->PSPR;
        break;
    case GAR_DQTR:
        val = pGa->DQTR;
        break;
    case GAR_R_BCFR:
        val = pGa->BCFR;
        break;
    case GAR_R_PCFR:
        val = pGa->PCFR;
        break;
    case GAR_GACFR:
        val = pGa->GACFR;
        break;
    case GAR_GACR:
        val = pGa->GACR;
        break;
    case GAR_STREG:
        val = pGa->STREG;
        break;
    case GAR_IDCFR:
        val = pGa->IDCFR;
        break;
    case GAR_DAMSB:
        val = pGa->DAMSB;
        break;
    case GAR_DALSB:
        val = pGa->DALSB;
        break;
    case GAR_VPTR2:
        val = pGa->VPTR2;
        break;
    case GAR_VPTR1:
        val = pGa->VPTR1;
        break;
    case GAR_VPTR0:
        val = pGa->VPTR0;
        break;
    case GAR_RFMSB:
    case GAR_RFLSB:
        val = elGaDataRead(pThis, pGa);
        break;
    default:
        Assert(0);
        break;
    }

    Log2Func(("#%d: addr=%#06x val=%#04x\n", pThis->iInstance, addr, val & 0xff));
    return val;
}


/**
 * @callback_method_impl{FNIOMIOPORTIN}
 */
static DECLCALLBACK(VBOXSTRICTRC)
neIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t *pu32, unsigned cb)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc    = VINF_SUCCESS;
    int             reg   = Port & 0xf;
    uint8_t         u8Lo, u8Hi = 0;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIORead), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1:
            *pu32 = neIoRead(pDevIns, pThis, reg);
            break;
        case 2:
            /* Manually split word access if necessary if it's an NE1000. Perhaps overkill. */
            if (pThis->uDevType == DEV_NE1000)
            {
                u8Lo = neIoRead(pDevIns, pThis, reg);
                if (reg < 0xf)  // This logic is not entirely accurate (wraparound).
                    u8Hi = neIoRead(pDevIns, pThis, reg + 1);
                *pu32 = RT_MAKE_U16(u8Lo, u8Hi);
            }
            else
                *pu32 = neIoRead(pDevIns, pThis, reg);
            break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                   "neIOPortRead: unsupported operation size: offset=%#10x cb=%u\n",
                                   Port, cb);
    }

    Log2Func(("#%d: NE Port=%RTiop *pu32=%#RX32 cb=%d rc=%Rrc\n", pThis->iInstance, Port, *pu32, cb, rc));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIORead), a);
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTIN}
 */
static DECLCALLBACK(VBOXSTRICTRC)
wdIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t *pu32, unsigned cb)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc    = VINF_SUCCESS;
    int             reg   = Port & 0xf;
    uint8_t         u8Lo, u8Hi = 0;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIORead), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1:
            *pu32 = wdIoRead(pThis, reg);
            break;
        case 2:
            /* Manually split word access. */
            u8Lo = wdIoRead(pThis, reg);
            if (reg < 0xf)  // This logic is not entirely accurate (wraparound).
                u8Hi = wdIoRead(pThis, reg + 1);
            *pu32 = RT_MAKE_U16(u8Lo, u8Hi);
            break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                   "wdIOPortRead: unsupported operation size: offset=%#10x cb=%u\n",
                                   Port, cb);
    }

    Log2Func(("#%d: WD Port=%RTiop *pu32=%#RX32 cb=%d rc=%Rrc\n", pThis->iInstance, Port, *pu32, cb, rc));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIORead), a);
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTIN}
 */
static DECLCALLBACK(VBOXSTRICTRC)
elIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t *pu32, unsigned cb)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc    = VINF_SUCCESS;
    int             reg   = Port & 0xf;
    uint8_t         u8Lo, u8Hi = 0;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIORead), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1:
            *pu32 = elGaIoRead(pThis, reg);
            break;
        case 2:
            /* Manually split word access. */
            u8Lo = elGaIoRead(pThis, reg);
            if (reg < 0xf)  // This logic is not entirely accurate (wraparound).
                u8Hi = elGaIoRead(pThis, reg + 1);
            *pu32 = RT_MAKE_U16(u8Lo, u8Hi);
            break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                   "elIOPortRead: unsupported operation size: offset=%#10x cb=%u\n",
                                   Port, cb);
    }

    Log2Func(("#%d: EL Port=%RTiop *pu32=%#RX32 cb=%d rc=%Rrc\n", pThis->iInstance, Port, *pu32, cb, rc));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIORead), a);
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTIN}
 */
static DECLCALLBACK(VBOXSTRICTRC)
dp8390CoreIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t *pu32, unsigned cb)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc    = VINF_SUCCESS;
    int             reg   = Port & 0xf;
    uint8_t         u8Lo, u8Hi;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIORead), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1:
            *pu32 = dp8390CoreRead(pDevIns, pThis, reg);
            break;
        case 2:
            /* Manually split word access. */
            u8Lo = dp8390CoreRead(pDevIns, pThis, reg + 0);
            /* This logic is not entirely accurate. */
            if (reg < 0xf)
                u8Hi = dp8390CoreRead(pDevIns, pThis, reg + 1);
            else
                u8Hi = 0;
            *pu32 = RT_MAKE_U16(u8Lo, u8Hi);
            break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                   "dp8390CoreIOPortRead: unsupported operation size: offset=%#10x cb=%u\n",
                                   Port, cb);
    }

    Log2Func(("#%d: Port=%RTiop *pu32=%#RX32 cb=%d rc=%Rrc\n", pThis->iInstance, Port, *pu32, cb, rc));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIORead), a);
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC)
neIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t u32, unsigned cb)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc    = VINF_SUCCESS;
    int             reg   = Port & 0xf;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1:
            rc = dpNeIoWrite(pDevIns, pThis, Port, RT_LOBYTE(u32));
            break;
        case 2:
            /* Manually split word access if necessary. */
            if (pThis->uDevType == DEV_NE2000)
            {
                rc = dpNeIoWrite(pDevIns, pThis, Port, RT_LOWORD(u32));
            }
            else
            {
                rc = dpNeIoWrite(pDevIns, pThis, reg + 0, RT_LOBYTE(u32));
                if (RT_SUCCESS(rc) && (reg < 0xf))
                    rc = dpNeIoWrite(pDevIns, pThis, reg + 1, RT_HIBYTE(u32));
            }
            break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                   "neIOPortWrite: unsupported operation size: offset=%#10x cb=%u\n",
                                   Port, cb);
    }

    Log2Func(("#%d: NE Port=%RTiop u32=%#RX32 cb=%d rc=%Rrc\n", pThis->iInstance, Port, u32, cb, rc));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC)
wdIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t u32, unsigned cb)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc    = VINF_SUCCESS;
    int             reg   = Port & 0xf;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1:
            rc = wdIoWrite(pDevIns, pThis, Port, RT_LOBYTE(u32));
            break;
        case 2:
            /* Manually split word access. */
            rc = wdIoWrite(pDevIns, pThis, reg + 0, RT_LOBYTE(u32));
            if (RT_SUCCESS(rc) && (reg < 0xf))
                rc = wdIoWrite(pDevIns, pThis, reg + 1, RT_HIBYTE(u32));
            break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                   "wdIOPortWrite: unsupported operation size: offset=%#10x cb=%u\n",
                                   Port, cb);
    }

    Log2Func(("#%d: WD Port=%RTiop u32=%#RX32 cb=%d rc=%Rrc\n", pThis->iInstance, Port, u32, cb, rc));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC)
elIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t u32, unsigned cb)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc    = VINF_SUCCESS;
    int             reg   = Port & 0xf;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1:
            rc = elGaIoWrite(pDevIns, pThis, Port, RT_LOBYTE(u32));
            break;
        case 2:
            /* Manually split word access. */
            rc = elGaIoWrite(pDevIns, pThis, reg + 0, RT_LOBYTE(u32));
            if (RT_SUCCESS(rc) && (reg < 0xf))
                rc = elGaIoWrite(pDevIns, pThis, reg + 1, RT_HIBYTE(u32));
            break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                   "elIOPortWrite: unsupported operation size: offset=%#10x cb=%u\n",
                                   Port, cb);
    }

    Log2Func(("#%d: EL Port=%RTiop u32=%#RX32 cb=%d rc=%Rrc\n", pThis->iInstance, Port, u32, cb, rc));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC)
dp8390CoreIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t u32, unsigned cb)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc    = VINF_SUCCESS;
    int             reg   = Port & 0xf;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    RT_NOREF_PV(pvUser);

    switch (cb)
    {
        case 1:
            rc = dp8390CoreWrite(pDevIns, pThis, reg, RT_LOBYTE(u32));
            break;
        case 2:
            /* Manually split word access. */
            rc = dp8390CoreWrite(pDevIns, pThis, reg + 0, RT_LOBYTE(u32));
            if (!RT_SUCCESS(rc))
                break;
            rc = dp8390CoreWrite(pDevIns, pThis, reg + 1, RT_HIBYTE(u32));
            break;
        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                   "dp8390CoreIOPortWrite: unsupported operation size: offset=%#10x cb=%u\n",
                                   Port, cb);
    }

    Log2Func(("#%d: Port=%RTiop u32=%#RX32 cb=%d rc=%Rrc\n", pThis->iInstance, Port, u32, cb, rc));
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    return rc;
}


#if 0
/**
 * @callback_method_impl{FNIOMMMIONEWFILL,
 * Local RAM write hook\, to be called from IOM. This is the advanced version of
 * wdMemWrite function.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
dpWdMmioFill(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, uint32_t u32Item, unsigned cbItem, unsigned cItems)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

    !!
    return VINF_SUCCESS
}
#endif


/**
 * @callback_method_impl{FNIOMMMIONEWREAD,
 * Local RAM read hook\, to be called from IOM.}
 */
static DECLCALLBACK(VBOXSTRICTRC) wdMemRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    uint8_t         *pbData = (uint8_t *)pv;
    NOREF(pvUser);

//    STAM_PROFILE_START(&pThis->CTX_MID_Z(Stat,MemoryRead), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

    if (pThis->ctrl1.MEME)
    {
        Log3Func(("#%d: Reading %u bytes from address %X: [%.*Rhxs]\n", pThis->iInstance, cb, off, cb, &pThis->abLocalRAM[off & DPNIC_MEM_MASK]));
        while (cb-- > 0)
            *pbData++ = pThis->abLocalRAM[off++ & DPNIC_MEM_MASK];
    }
    else
        memset(pv, 0xff, cb);

//    STAM_PROFILE_STOP(&pThis->CTX_MID_Z(Stat,MemoryRead), a);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMMMIONEWWRITE,
 * Local RAM write hook\, to be called from IOM.}
 */
static DECLCALLBACK(VBOXSTRICTRC) wdMemWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDPNICSTATE     pThis  = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    uint8_t const   *pbSrc = (uint8_t const *)pv;
    NOREF(pvUser);

//    STAM_PROFILE_START(&pThis->CTX_MID_Z(Stat,MemoryWrite), a);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

    if (pThis->ctrl1.MEME)
    {
        Log3Func(("#%d: Writing %u bytes to address %X: [%.*Rhxs]\n", pThis->iInstance, cb, off, cb, pbSrc));
        while (cb-- > 0)
            pThis->abLocalRAM[off++ & DPNIC_MEM_MASK] = *pbSrc++;
    }

//    STAM_PROFILE_STOP(&pThis->CTX_MID_Z(Stat,MemoryWrite), a);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD,
 * Local RAM read hook\, to be called from IOM.}
 */
static DECLCALLBACK(VBOXSTRICTRC) elMemRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    uint8_t         *pbData = (uint8_t *)pv;
    NOREF(pvUser);

    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

    if (pThis->ga.gacfr.rsel)
    {
        Log3Func(("#%d: Reading %u bytes from address %X\n", pThis->iInstance, cb, off));
        while (cb-- > 0)
            *pbData++ = pThis->abLocalRAM[off++ & DPNIC_MEM_MASK];
    }
    else
    {
        Log3Func(("#%d: Ignoring read of %u bytes from address %X\n", pThis->iInstance, cb, off));
        memset(pv, 0xff, cb);
    }
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE,
 * Local RAM write hook\, to be called from IOM.}
 */
static DECLCALLBACK(VBOXSTRICTRC) elMemWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDPNICSTATE     pThis  = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    uint8_t const   *pbSrc = (uint8_t const *)pv;
    NOREF(pvUser);

    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

    if (pThis->ga.gacfr.rsel)
    {
        Log3Func(("#%d: Writing %u bytes to address %X\n", pThis->iInstance, cb, off));
        while (cb-- > 0)
            pThis->abLocalRAM[off++ & DPNIC_MEM_MASK] = *pbSrc++;
    }
    else
    {
        Log3Func(("#%d: Ignoring write of %u bytes to address %X\n", pThis->iInstance, cb, off));
    }
    return VINF_SUCCESS;
}


#ifdef IN_RING3

/* Shamelessly stolen from DevDMA.cpp */

/* Test the decrement bit of mode register. */
#define IS_MODE_DEC(c)      ((c) & 0x20)
/* Test the auto-init bit of mode register. */
#define IS_MODE_AI(c)       ((c) & 0x10)
/* Extract the transfer type bits of mode register. */
#define GET_MODE_XTYP(c)    (((c) & 0x0c) >> 2)

/* DMA transfer modes. */
enum {
    DMODE_DEMAND,   /* Demand transfer mode. */
    DMODE_SINGLE,   /* Single transfer mode. */
    DMODE_BLOCK,    /* Block transfer mode. */
    DMODE_CASCADE   /* Cascade mode. */
};

/* DMA transfer types. */
enum {
    DTYPE_VERIFY,   /* Verify transfer type. */
    DTYPE_WRITE,    /* Write transfer type. */
    DTYPE_READ,     /* Read transfer type. */
    DTYPE_ILLEGAL   /* Undefined. */
};

static DECLCALLBACK(uint32_t) elnk3R3DMAXferHandler(PPDMDEVINS pDevIns, void *opaque,
                                                    unsigned nchan, uint32_t dma_pos, uint32_t dma_len)
{
    PDPNICSTATE     pThis = (PDPNICSTATE)opaque;
    int             dma_mode;
    int             dma_type;
    uint16_t        cbToXfer;
    uint32_t        cbXferred = 0;
    uint16_t        uDmaAddr;
    int             rc;

    /*
     * The 3C503 EtherLink II uses DMA as an alternative to shared RAM
     * or PIO. The Gate Array tracks its own current DMA address within
     * the adapter's local address space.
     */
    dma_mode = PDMDevHlpDMAGetChannelMode(pDevIns, pThis->uIsaDma);
    dma_type = GET_MODE_XTYP(dma_mode);
    uDmaAddr = pThis->ga.CDADR;
    cbToXfer = dma_len;
    LogFlowFunc(("dma_mode=%d, dma_type=%d, dma_pos=%u, dma_len=%u, cdadr=%04X\n", dma_mode, dma_type, dma_pos, dma_len, uDmaAddr));

    /* Skip any accesses below local memory start. */
    if ((0x2000 > 0) && (uDmaAddr < 0x2000))  /// @todo Should keep track in variables
    {
        uint16_t    cbToSkip = 0x2000 - uDmaAddr;

        uDmaAddr += cbToSkip;
        /// @todo Should this write junk to host memory when reading from device?
        if (cbToSkip < cbToXfer)
        {
            cbToXfer -= cbToSkip;
            Assert(uDmaAddr == 0x2000);
            LogFunc(("DMA skipping %u bytes!\n", cbToSkip));
        }
        else
        {
            cbToXfer = 0;   /* Transfer entirely below valid address range. */
            LogFunc(("DMA below valid address range!\n"));
        }
    }

    if (cbToXfer)
    {
        uint16_t    cbToSkip = 0;

        /* Clip transfer size so it falls within local RAM. */
        if ((uDmaAddr - 0x2000 + cbToXfer) > (int)sizeof(pThis->abLocalRAM))
        {
            /* Calculate how much to skip anything at the end. */
            cbToSkip = sizeof(pThis->abLocalRAM) - (0x2000 - uDmaAddr + cbToXfer);
            LogFunc(("DMA above valid address range uDmaAddr=%04X cbToXfer=%u cbToSkip=%u!\n", uDmaAddr, cbToXfer, cbToSkip));
            cbToXfer -= cbToSkip;
        }

        if (dma_type == DTYPE_WRITE)
        {
            /* Write transfer type. Reading from device, writing to memory. */
            if (!pThis->ga.gacr.ddir)
            {
                Log2Func(("DMAWriteMemory uDmaAddr=%04X cbToXfer=%u\n", uDmaAddr, cbToXfer));
                rc = PDMDevHlpDMAWriteMemory(pDevIns, nchan,
                                             &pThis->abLocalRAM[uDmaAddr - 0x2000],
                                             dma_pos, cbToXfer, &cbXferred);
                AssertMsgRC(rc, ("DMAWriteMemory -> %Rrc\n", rc));
            }
            else
            {
                // Do nothing, direction does not match.
                /// @todo Bug in DevDMA?
                LogFunc(("DTYPE_WRITE but GACR.ddir set, do nothing!\n"));
            }
        }
        else
        {
            /* Read of Verify transfer type. Reading from memory, writing to device. */
            if (pThis->ga.gacr.ddir)
            {
                Log2Func(("DMAReadMemory uDmaAddr=%04X cbToXfer=%u\n", uDmaAddr, cbToXfer));
                rc = PDMDevHlpDMAReadMemory(pDevIns, nchan,
                                            &pThis->abLocalRAM[uDmaAddr - 0x2000],
                                            dma_pos, cbToXfer, &cbXferred);
                AssertMsgRC(rc, ("DMAReadMemory -> %Rrc\n", rc));
            }
            else
            {
                // Do nothing, direction does not match.
                /// @todo Bug in DevDMA?
                LogFunc(("DTYPE_READ but GACR.ddir clear, do nothing!\n"));
            }
        }

        /* NB: This might wrap. In theory it might wrap back to valid
         * memory but... just no.
         */
        /// @todo Actually... what would really happen?
        uDmaAddr += cbToXfer + cbToSkip;
    }
    Log2Func(("After DMA transfer: uDmaAddr=%04X, cbXferred=%u\n", uDmaAddr, cbXferred));

    /* Advance the DMA address and see if transfer completed (it almost certainly did). */
    if (1)
    {
        Log2Func(("DMA completed\n"));
        PDMDevHlpDMASetDREQ(pDevIns, pThis->uIsaDma, 0);
        pThis->ga.streg.dtc = 1;
        pThis->ga.fGaIrq    = true;
        dp8390CoreUpdateIrq(pDevIns, pThis);
    }
    else
    {
        LogFunc(("DMA continuing: uDmaAddr=%04X, cbXferred=%u\n", uDmaAddr, cbXferred));
        PDMDevHlpDMASchedule(pDevIns);
    }

    /* Returns the updated transfer count. */
    return dma_pos + dma_len;
}


/* -=-=-=-=-=- Timer Callbacks -=-=-=-=-=- */

/**
 * @callback_method_impl{FNTMTIMERDEV, Restore timer callback}
 *
 * This is only called when we restore a saved state and temporarily
 * disconnected the network link to inform the guest that network connections
 * should be considered lost.
 */
static DECLCALLBACK(void) dpNicR3TimerRestore(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    RT_NOREF(pvUser);
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertReleaseRC(rc);

    rc = VERR_GENERAL_FAILURE;

    /* The DP8390 based cards have no concept of link state. Reporting collisions on all transmits
     * is the best approximation of a disconnected cable that we can do. Some drivers (3C503) warn
     * of possible disconnected cable, some don't. Many cards with DP8390 chips had permanently
     * attached cables (AUI or BNC) and their drivers do not expect cables to be disconnected and
     * re-connected at runtime. Guests which are waiting for a receive have no way to notice any
     * problem, therefore we only postpone restoring a link a couple of times, and then reconnect
     * regardless of whether the guest noticed anything or not.
     */
    if (   (pThis->cLinkDownReported <= DPNIC_MAX_LINKDOWN_REPORTED)
        && (pThis->cLinkRestorePostponed <= DPNIC_MAX_LINKRST_POSTPONED))
        rc = PDMDevHlpTimerSetMillies(pDevIns, hTimer, 1500);
    if (RT_FAILURE(rc))
    {
        pThis->fLinkTempDown = false;
        if (pThis->fLinkUp)
        {
            LogRel(("DPNIC#%d: The link is back up again after the restore.\n",
                    pThis->iInstance));
            LogFunc(("#%d: cLinkDownReported=%d\n", pThis->iInstance, pThis->cLinkDownReported));
            pThis->Led.Actual.s.fError = 0;
        }
    }
    else
    {
        LogFunc(("#%d: cLinkDownReported=%d, cLinkRestorePostponed=%d, wait another 1500ms...\n",
                 pThis->iInstance, pThis->cLinkDownReported, pThis->cLinkRestorePostponed));
        pThis->cLinkRestorePostponed++;
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/* -=-=-=-=-=- Debug Info Handler -=-=-=-=-=- */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) dpNicR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    bool            fRecvBuffer  = false;
    bool            fSendBuffer  = false;
    unsigned        uFreePages;
    DP8390CORE      *pCore = &pThis->core;
    const char      *aszModels[] = {"NE1000", "NE2000", "WD8003E", "WD8013E", "3C503"};

    /*
     * Parse args.
     */
    if (pszArgs)
    {
        fRecvBuffer  = strstr(pszArgs, "verbose") || strstr(pszArgs, "recvbuf");
        fSendBuffer  = strstr(pszArgs, "verbose") || strstr(pszArgs, "sendbuf");
    }

    /*
     * Show device information.
     */
    pHlp->pfnPrintf(pHlp, "DPNIC #%d: %s port=%RTiop IRQ=%u",
                    pThis->iInstance,
                    aszModels[pThis->uDevType],
                    pThis->IOPortBase,
                    pThis->uIsaIrq);
    if (pThis->MemBase)
        pHlp->pfnPrintf(pHlp, " mem=%05X-%05X", pThis->MemBase, pThis->MemBase + pThis->cbMemSize - 1);
    if (pThis->uIsaDma)
        pHlp->pfnPrintf(pHlp, " DMA=%u", pThis->uIsaDma);
    pHlp->pfnPrintf(pHlp, " mac-cfg=%RTmac%s %s\n",
                    &pThis->MacConfigured,
                    pDevIns->fR0Enabled ? " RZ" : "",
                    pThis->fDriverAttached ? "attached" : "unattached!");

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_INTERNAL_ERROR); /* Take it here so we know why we're hanging... */
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    pHlp->pfnPrintf(pHlp, "\nDP3890 NIC Core\n");
    pHlp->pfnPrintf(pHlp, "   CR=%02X: %s%s%s RD=%d PS=%d\n", pCore->CR,
                    pCore->cr.STP ? "STP " : "",
                    pCore->cr.STA ? "STA " : "",
                    pCore->cr.TXP ? "TXP " : "",
                    pCore->cr.RD, pCore->cr.PS);
    pHlp->pfnPrintf(pHlp, "  ISR=%02X: %s%s%s%s%s%s%s%s\n", pCore->ISR,
                    pCore->isr.PRX ? "PRX " : "",
                    pCore->isr.PTX ? "PTX " : "",
                    pCore->isr.RXE ? "RXE " : "",
                    pCore->isr.TXE ? "TXE " : "",
                    pCore->isr.OVW ? "OVW " : "",
                    pCore->isr.CNT ? "CNT " : "",
                    pCore->isr.RDC ? "RDC " : "",
                    pCore->isr.RST ? "RST " : "");
    pHlp->pfnPrintf(pHlp, "  IMR=%02X: %s%s%s%s%s%s%s%s\n", pCore->IMR,
                    pCore->imr.PRXE ? "PRXE " : "",
                    pCore->imr.PTXE ? "PTXE " : "",
                    pCore->imr.RXEE ? "RXEE " : "",
                    pCore->imr.TXEE ? "TXEE " : "",
                    pCore->imr.OVWE ? "OVWE " : "",
                    pCore->imr.CNTE ? "CNTE " : "",
                    pCore->imr.RDCE ? "RDCE " : "",
                    pCore->imr.res ? "Reserved bit set!!" : "");
    pHlp->pfnPrintf(pHlp, "  DCR=%02X: %s%s%s%s%sFT=%d %s\n", pCore->DCR,
                    pCore->dcr.WTS ? "WTS " : "",
                    pCore->dcr.BOS ? "BOS " : "",
                    pCore->dcr.LAS ? "LAS " : "",
                    pCore->dcr.LS  ? "LS "  : "",
                    pCore->dcr.ARM ? "ARM " : "",
                    pCore->dcr.FT,
                    pCore->dcr.res ? "Reserved bit set!!" : "");
    pHlp->pfnPrintf(pHlp, "  TCR=%02X: %sLB=%d %s%s\n", pCore->TCR,
                    pCore->tcr.CRC  ? "CRC " : "",
                    pCore->tcr.LB,
                    pCore->tcr.ATD  ? "ATD " : "",
                    pCore->tcr.OFST ? "OFST" : "");
    pHlp->pfnPrintf(pHlp, "  TSR=%02X: %s%s%s%s%s%s%s%s\n", pCore->TSR,
                    pCore->tsr.PTX ? "PTX " : "",
                    pCore->tsr.DFR ? "DFR " : "",
                    pCore->tsr.COL ? "COL " : "",
                    pCore->tsr.ABT ? "ABT " : "",
                    pCore->tsr.CRS ? "CRS " : "",
                    pCore->tsr.FU  ? "FU "  : "",
                    pCore->tsr.CDH ? "CDH " : "",
                    pCore->tsr.OWC ? "OWC " : "");
    pHlp->pfnPrintf(pHlp, "  RCR=%02X: %s%s%s%s%s%s\n", pCore->RCR,
                    pCore->rcr.SEP ? "SEP " : "",
                    pCore->rcr.AR  ? "AR "  : "",
                    pCore->rcr.AB  ? "AB "  : "",
                    pCore->rcr.AM  ? "AM "  : "",
                    pCore->rcr.PRO ? "PRO " : "",
                    pCore->rcr.MON ? "MON " : "");
    pHlp->pfnPrintf(pHlp, "  RSR=%02X: %s%s%s%s%s%s%s%s\n", pCore->RSR,
                    pCore->rsr.PRX ? "PRX " : "",
                    pCore->rsr.CRC ? "CRC " : "",
                    pCore->rsr.FAE ? "FAE " : "",
                    pCore->rsr.FO  ? "FO "  : "",
                    pCore->rsr.MPA ? "MPA " : "",
                    pCore->rsr.PHY ? "PHY " : "",
                    pCore->rsr.DIS ? "DIS " : "",
                    pCore->rsr.DFR ? "DFR " : "");
    pHlp->pfnPrintf(pHlp, "  ActIntSrc: %02X\n", pCore->ISR & pCore->IMR);
    pHlp->pfnPrintf(pHlp, "  Receiving: %s%s%s%s%s%s\n",
                    pCore->rcr.AB  ? "Broadcast " : "",
                    pCore->rcr.AM  ? "Multicast " : "",
                    pCore->rcr.PRO ? "Promiscuous " : "",
                    pCore->rcr.MON ? "Monitor " : "",
                    pCore->cr.STA  ? "Started " : "Not started ",
                    pCore->isr.RST ? "Reset!" : "");

    /* Dump the currently programmed station address. */
    pHlp->pfnPrintf(pHlp, "  MAC Addr : %RTmac\n", &pCore->pg1.PAR);

    /* Dump the currently programmed multicast filter. */
    pHlp->pfnPrintf(pHlp, "  Multicast: %02X:%02X:%02X:%02X %02X:%02X:%02X:%02X\n",
                    pCore->pg1.MAR[0], pCore->pg1.MAR[1], pCore->pg1.MAR[2], pCore->pg1.MAR[3],
                    pCore->pg1.MAR[4], pCore->pg1.MAR[5], pCore->pg1.MAR[6], pCore->pg1.MAR[7]);

    /* Dump the DMA state. */
    pHlp->pfnPrintf(pHlp, "  Local DMA : TPSR=%02X00 TBCR=%04X CLDA=%04X\n",
                    pCore->TPSR, pCore->TBCR, pCore->CLDA);
    pHlp->pfnPrintf(pHlp, "            : PSTART=%02X00 PSTOP=%02X00 CURR=%02X00 BNRY=%02X00\n",
                    pCore->PSTART, pCore->PSTOP, pCore->CURR, pCore->BNRY);
    pHlp->pfnPrintf(pHlp, "  Remote DMA: RSAR=%04X RBCR=%04X CRDA=%04X\n",
                    pCore->RSAR, pCore->RBCR, pCore->CRDA);

    /* Try to figure out how much available space there is in the receive ring. */
    if (pCore->BNRY <= pCore->CURR)
        uFreePages = pCore->PSTOP - pCore->PSTART - (pCore->CURR - pCore->BNRY);
    else
        uFreePages = pCore->BNRY - pCore->CURR;
    pHlp->pfnPrintf(pHlp, "  Estimated %u free pages (%u bytes) in receive ring\n", uFreePages, uFreePages * 256);

    if (pThis->fMaybeOutOfSpace)
        pHlp->pfnPrintf(pHlp, "  Waiting for receive space\n");
    if (pThis->fLinkTempDown)
    {
        pHlp->pfnPrintf(pHlp, "  Link down count %d\n", pThis->cLinkDownReported);
        pHlp->pfnPrintf(pHlp, "  Postpone count  %d\n", pThis->cLinkRestorePostponed);
    }

    if ((pThis->uDevType == DEV_WD8003) || (pThis->uDevType == DEV_WD8013))
    {
        /* Dump the WD specific registers. */
        pHlp->pfnPrintf(pHlp, "\nWD80x3 Control Registers\n");
        pHlp->pfnPrintf(pHlp, "  CTRL1=%02X: %s%s A18-A13=%02X\n", pThis->CTRL1,
                        pThis->ctrl1.RESET ? "RESET " : "",
                        pThis->ctrl1.MEME  ? "MEME " : "",
                        pThis->ctrl1.A13_18);
        pHlp->pfnPrintf(pHlp, "  CTRL2=%02X: %s%s A23-A19=%02X\n", pThis->CTRL2,
                        pThis->ctrl2.M16  ? "M16 "  : "",
                        pThis->ctrl2.MEMW ? "MEMW " : "",
                        pThis->ctrl2.A19_23);
    }

    if (pThis->uDevType == DEV_3C503)
    {
        PEL_GA      pGa = &pThis->ga;

        /* Dump the Gate Array state. */
        pHlp->pfnPrintf(pHlp, "\n3C503 ASIC Gate Array\n");
        pHlp->pfnPrintf(pHlp, "   PSTR=%02X00 PSPR=%02X00 cdadr=%04X\n",
                        pGa->PSTR, pGa->PSTR, pGa->CDADR);
        pHlp->pfnPrintf(pHlp, "   DQTR=%02X: tb=%d\n", pGa->DQTR,
                        pGa->dqtr.tb);
        pHlp->pfnPrintf(pHlp, "   BCFR=%02X PCFR=%02X\n",
                        pGa->BCFR, pGa->PCFR);
        pHlp->pfnPrintf(pHlp, "  GACFR=%02X: mbs=%d %s%s%s%s%s\n", pGa->GACFR,
                        pGa->gacfr.mbs,
                        pGa->gacfr.rsel ? "rsel " : "",
                        pGa->gacfr.test ? "test " : "",
                        pGa->gacfr.ows  ? "ows "  : "",
                        pGa->gacfr.tcm  ? "tcm "  : "",
                        pGa->gacfr.nim  ? "nim "  : "");
        pHlp->pfnPrintf(pHlp, "   GACR=%02X: %s%s%s%s%s%s%s%s\n", pGa->GACR,
                        pGa->gacr.rst   ? "rst "   : "",
                        pGa->gacr.xsel  ? "xsel "  : "",
                        pGa->gacr.ealo  ? "ealo "  : "",
                        pGa->gacr.eahi  ? "eahi "  : "",
                        pGa->gacr.share ? "share " : "",
                        pGa->gacr.dbsel ? "dbsel " : "",
                        pGa->gacr.ddir  ? "ddir "  : "",
                        pGa->gacr.start ? "start " : "");
        pHlp->pfnPrintf(pHlp, "  STREG=%02X: rev=%d %s%s%s%s%s\n", pGa->STREG,
                        pGa->streg.rev,
                        pGa->streg.dip   ? "dip "   : "",
                        pGa->streg.dtc   ? "dtc "   : "",
                        pGa->streg.oflw  ? "oflw "  : "",
                        pGa->streg.uflw  ? "uflw "  : "",
                        pGa->streg.dprdy ? "dprdy " : "");
        pHlp->pfnPrintf(pHlp, "  IDCFR=%02X: %s%s%s%s%s%s%s\n", pGa->IDCFR,
                        pGa->idcfr.drq1 ? "drq1 " : "",
                        pGa->idcfr.drq2 ? "drq2 " : "",
                        pGa->idcfr.drq3 ? "drq3 " : "",
                        pGa->idcfr.irq2 ? "irq2 " : "",
                        pGa->idcfr.irq3 ? "irq3 " : "",
                        pGa->idcfr.irq4 ? "irq4 " : "",
                        pGa->idcfr.irq5 ? "irq5 " : "");
        pHlp->pfnPrintf(pHlp, "  DALSB=%02X DAMSB=%02X addr=%04X\n",
                        pGa->DALSB, pGa->DAMSB,
                        RT_MAKE_U16(pGa->DALSB, pGa->DAMSB));
        pHlp->pfnPrintf(pHlp, "  VPTR0=%02X VPTR1=%02X VPTR2=%02X, VPTR=%X\n",
                        pGa->VPTR0, pGa->VPTR1, pGa->VPTR2,
                        (pGa->VPTR2 << 12) | (pGa->VPTR1 << 4) | (pGa->VPTR0 >> 4));



    }

    /* Dump the beginning of the send buffer. */
    if (fSendBuffer)
    {
        pHlp->pfnPrintf(pHlp, "Send buffer (start at %u):\n", 0);
        unsigned dump_end = RT_MIN(0 + 64, sizeof(pThis->abLocalRAM) - 16);
        for (unsigned ofs = 0; ofs < dump_end; ofs += 16)
            pHlp->pfnPrintf(pHlp, "  %04X: %Rhxs\n", ofs, &pThis->abLocalRAM[ofs]);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/* -=-=-=-=-=- Helper(s) -=-=-=-=-=- */


static void dpNicR3HardReset(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    LogFlowFunc(("#%d:\n", pThis->iInstance));

    /* Initialize the PROM. Covers both NE1000 and NE2000. */
    Assert(sizeof(pThis->MacConfigured) == 6);
    memset(pThis->aPROM, 0, sizeof(pThis->aPROM));
    /* The first 6 bytes of PROM always contain the configured MAC address. */
    memcpy(&pThis->aPROM[0x00], &pThis->MacConfigured, sizeof(pThis->MacConfigured));

    if ((pThis->uDevType == DEV_NE1000) || (pThis->uDevType == DEV_NE2000))
    {
        /* The NE1000/NE2000 repeats the MAC address and also includes BB/WW signature. */
        memcpy(&pThis->aPROM[0x10], &pThis->MacConfigured, sizeof(pThis->MacConfigured));
        pThis->aPROM[0x0E] = pThis->aPROM[0x0F] = 'W';  /* Word-wide. */
        pThis->aPROM[0x1E] = pThis->aPROM[0x1F] = 'B';  /* Byte-wide. */
    }
    else if ((pThis->uDevType == DEV_WD8003) || (pThis->uDevType == DEV_WD8013))
    {
        /* The WD8003/WD8013 only uses 8 bytes of the PROM. The 7th byte
         * contains a board ID and the last byte is a checksum calculated
         * such that a two's complement sum of the 8 bytes equals FFh.
         */
        int     i;
        uint8_t sum;

        /* The board ID is 2 for 8003S, 3 for 8003E, 4 for 8003WT, 5 for 8013EBT. */
        pThis->aPROM[0x06] = 3;
        if (pThis->uDevType == DEV_WD8013)
            pThis->aPROM[0x06] = 5;

        for (i = 0, sum = 0; i < 7; ++i)
            sum += pThis->aPROM[i];

        pThis->aPROM[0x07] = 0xff - sum;
    }
    else if (pThis->uDevType == DEV_3C503)
    {
        const uint16_t  el_io_bases[]  = { 0x2E0, 0x2A0, 0x280, 0x250, 0x350, 0x330, 0x310, 0x300, 0 };
        const uint32_t  el_mem_bases[] = { 0xDC000, 0xD8000, 0xCC000, 0xC8000, 0 };
        int             i;

        /* Zap the Gate Array state. */
        memset(&pThis->ga, 0, sizeof(pThis->ga));

        /* Find the BCFR value. */
        for (i = 0; el_io_bases[i]; ++i)
        {
            if (pThis->IOPortBase == el_io_bases[i])
                break;
        }
        /// @todo Make sure we somehow disallow values that a 3C503 can't do
        if (i < 8)
            pThis->ga.BCFR = 1 << i;

        /* Find the PCFR value. */
        for (i = 0; el_mem_bases[i]; ++i)
        {
            if (pThis->MemBase == el_mem_bases[i])
                break;
        }
        /// @todo Make sure we somehow disallow values that a 3C503 can't do
        if (i < 4)
            pThis->ga.PCFR = RT_BIT(7) >> i;
    }

    /* Clear the local RAM. */
    memset(pThis->abLocalRAM, 0, sizeof(pThis->abLocalRAM));

    /* Wipe out all of the DP8390 core state. */
    memset(&pThis->core, 0, sizeof(pThis->core));

    dp8390CoreReset(pDevIns, pThis);
}

/**
 * Takes down the link temporarily if it's current status is up.
 *
 * This is used during restore and when replumbing the network link.
 *
 * The temporary link outage is supposed to indicate to the OS that all network
 * connections have been lost and that it for instance is appropriate to
 * renegotiate any DHCP lease.
 *
 * @param  pDevIns      The device instance data.
 * @param  pThis        The device state.
 */
static void dp8390TempLinkDown(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    if (pThis->fLinkUp)
    {
        pThis->fLinkTempDown = true;
        pThis->cLinkDownReported = 0;
        pThis->cLinkRestorePostponed = 0;
        pThis->Led.Asserted.s.fError = pThis->Led.Actual.s.fError = 1;
        int rc = PDMDevHlpTimerSetMillies(pDevIns, pThis->hTimerRestore, pThis->cMsLinkUpDelay);
        AssertRC(rc);
    }
}


/* -=-=-=-=-=- Saved State -=-=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC, Pass 0 only.}
 */
static DECLCALLBACK(int) dpNicLiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    RT_NOREF(uPass);
    PDPNICSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    pDevIns->pHlpR3->pfnSSMPutMem(pSSM, &pThis->MacConfigured, sizeof(pThis->MacConfigured));
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEPREP,
 *      Serializes the receive thread, it may be working inside the critsect.}
 */
static DECLCALLBACK(int) dpNicSavePrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    PDPNICSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRC(rc);
    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) dpNicSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDPNICSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    /* Start with saving the generic bits. */
    pHlp->pfnSSMPutBool(pSSM, pThis->fLinkUp);
    pHlp->pfnSSMPutBool(pSSM, pThis->fNicIrqActive);

    /* Continue with DP8390 core. */
    pHlp->pfnSSMPutU8(pSSM, pThis->core.CR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.DCR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.ISR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.IMR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.RCR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.RSR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.TCR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.TSR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.NCR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.TPSR);
    pHlp->pfnSSMPutU16(pSSM, pThis->core.TBCR);
    pHlp->pfnSSMPutU16(pSSM, pThis->core.CLDA);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.PSTART);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.PSTOP);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.CURR);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.BNRY);
    pHlp->pfnSSMPutU16(pSSM, pThis->core.RSAR);
    pHlp->pfnSSMPutU16(pSSM, pThis->core.RBCR);
    pHlp->pfnSSMPutU16(pSSM, pThis->core.CRDA);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.lnxtpp);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.rnxtpp);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.CNTR0);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.CNTR1);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.CNTR2);
    pHlp->pfnSSMPutMem(pSSM, &pThis->core.pg1.PAR, sizeof(pThis->core.pg1.PAR));
    pHlp->pfnSSMPutMem(pSSM, &pThis->core.pg1.MAR, sizeof(pThis->core.pg1.MAR));
    pHlp->pfnSSMPutU8(pSSM, pThis->core.fifo.rp);
    pHlp->pfnSSMPutU8(pSSM, pThis->core.fifo.wp);
    pHlp->pfnSSMPutMem(pSSM, &pThis->core.fifo.fifo, sizeof(pThis->core.fifo.fifo));

    /* Now the WD80x3 state. */
    pHlp->pfnSSMPutU8(pSSM, pThis->CTRL1);
    pHlp->pfnSSMPutU8(pSSM, pThis->CTRL2);

    /* Finally the 3C503-specific state. */
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.PSTR);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.PSPR);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.DQTR);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.BCFR);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.PCFR);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.GACFR);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.GACR);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.STREG);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.IDCFR);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.DAMSB);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.DALSB);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.VPTR2);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.VPTR1);
    pHlp->pfnSSMPutU8(pSSM, pThis->ga.VPTR0);
    pHlp->pfnSSMPutU16(pSSM, pThis->ga.CDADR);
    pHlp->pfnSSMPutBool(pSSM, pThis->ga.fGaIrq);

    /* Save the configured MAC address. */
    pHlp->pfnSSMPutMem(pSSM, &pThis->MacConfigured, sizeof(pThis->MacConfigured));

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADPREP},
 *      Serializes the receive thread, it may be working inside the critsect.}
 */
static DECLCALLBACK(int) dpNicLoadPrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDPNICSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    RT_NOREF(pSSM);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRC(rc);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);

    return rc;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) dpNicLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    PDPNICSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDPNICSTATECC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    if (SSM_VERSION_MAJOR_CHANGED(uVersion, DPNIC_SAVEDSTATE_VERSION))
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    if (uPass == SSM_PASS_FINAL)
    {
        /* Restore data, first the generic bits. */
        pHlp->pfnSSMGetBool(pSSM, &pThis->fLinkUp);
        pHlp->pfnSSMGetBool(pSSM, &pThis->fNicIrqActive);

        /* Now the DP8390 core. */
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.CR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.DCR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.ISR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.IMR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.RCR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.RSR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.TCR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.TSR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.NCR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.TPSR);
        pHlp->pfnSSMGetU16(pSSM, &pThis->core.TBCR);
        pHlp->pfnSSMGetU16(pSSM, &pThis->core.CLDA);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.PSTART);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.PSTOP);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.CURR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.BNRY);
        pHlp->pfnSSMGetU16(pSSM, &pThis->core.RSAR);
        pHlp->pfnSSMGetU16(pSSM, &pThis->core.RBCR);
        pHlp->pfnSSMGetU16(pSSM, &pThis->core.CRDA);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.lnxtpp);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.rnxtpp);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.CNTR0);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.CNTR1);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.CNTR2);
        pHlp->pfnSSMGetMem(pSSM, &pThis->core.pg1.PAR, sizeof(pThis->core.pg1.PAR));
        pHlp->pfnSSMGetMem(pSSM, &pThis->core.pg1.MAR, sizeof(pThis->core.pg1.MAR));
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.fifo.rp);
        pHlp->pfnSSMGetU8(pSSM, &pThis->core.fifo.wp);
        pHlp->pfnSSMGetMem(pSSM, &pThis->core.fifo.fifo, sizeof(pThis->core.fifo.fifo));

        /* WD80x3-specific state. */
        pHlp->pfnSSMGetU8(pSSM, &pThis->CTRL1);
        pHlp->pfnSSMGetU8(pSSM, &pThis->CTRL2);

        /* 3C503-specific state. */
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.PSTR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.PSPR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.DQTR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.BCFR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.PCFR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.GACFR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.GACR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.STREG);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.IDCFR);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.DAMSB);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.DALSB);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.VPTR2);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.VPTR1);
        pHlp->pfnSSMGetU8(pSSM, &pThis->ga.VPTR0);
        pHlp->pfnSSMGetU16(pSSM, &pThis->ga.CDADR);
        pHlp->pfnSSMGetBool(pSSM, &pThis->ga.fGaIrq);

        /* Set IRQ and DMA based on IDCFR if this is a 3C503. */
        if (pThis->uDevType == DEV_3C503)
        {
            pThis->uIsaIrq   = elGetIrqFromIdcfr(pThis->ga.IDCFR);
            pThis->uElIsaDma = elGetDrqFromIdcfr(pThis->ga.IDCFR);
        }
    }

    /* check config */
    RTMAC       Mac;
    int rc = pHlp->pfnSSMGetMem(pSSM, &Mac, sizeof(Mac));
    AssertRCReturn(rc, rc);
    if (    memcmp(&Mac, &pThis->MacConfigured, sizeof(Mac))
        && (uPass == 0 || !PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns)) )
        LogRel(("DPNIC#%u: The mac address differs: config=%RTmac saved=%RTmac\n", pThis->iInstance, &pThis->MacConfigured, &Mac));

    if (uPass == SSM_PASS_FINAL)
    {
        /* update promiscuous mode. */
        if (pThisCC->pDrv)
            pThisCC->pDrv->pfnSetPromiscuousMode(pThisCC->pDrv, 0 /* promiscuous enabled */);

        /* Indicate link down to the guest OS that all network connections have
           been lost, unless we've been teleported here. */
        if (!PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns))
            dp8390TempLinkDown(pDevIns, pThis);
    }

    return VINF_SUCCESS;
}


/* -=-=-=-=-=- DPNICSTATE::INetworkDown -=-=-=-=-=- */

/**
 * Check if the device/driver can receive data now.
 *
 * Worker for dpNicNet_WaitReceiveAvail().  This must be called before
 * the pfnRecieve() method is called.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The device instance data.
 */
static int dp8390CanReceive(PPDMDEVINS pDevIns, PDPNICSTATE pThis)
{
    DP8390CORE  *pCore = &pThis->core;
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertReleaseRC(rc);

    rc = VINF_SUCCESS;

    /*
     * The card has typically room for several full-size Ethernet frames but
     * the buffers can overflow. We cheat a bit and try to hold off when it
     * looks like there is temporarily not enough buffer spave.
     *
     * If the receiver is disabled, accept packets and drop them to avoid
     * pile-ups. If the receiver is enabled, take a closer look.
     */
    if (pCore->cr.STA && !pCore->cr.STP)
    {
        /* Receiver is enabled. Find out if we're low on buffer space.
         * But if the receive buffer isn't at least 4K big (16 pages),
         * don't bother. Typically there will be 5K or more in the
         * receive buffer.
         */
        if (pCore->PSTART + 16 <= pCore->PSTOP)
        {
            uint16_t    free_pages;

            /* Free space is between BNRY (host's read pointer) and CURR
             * (NIC's write pointer).
             */
            if (pCore->BNRY <= pCore->CURR)
            {
                /* Free space wraps around. This might technically give
                 * the wrong answer if the buffer is empty (BNRY = CURR)
                 * but in that case there's plenty of room anyway.
                 */
                free_pages = pCore->PSTOP - pCore->PSTART - (pCore->CURR - pCore->BNRY);
            }
            else
            {
                /* Free space does not wrap. */
                free_pages = pCore->BNRY - pCore->CURR;
            }
            Log2Func(("#%d: %u free pages (%u bytes)\n", pThis->iInstance, free_pages, free_pages * 256));

            /* Six pages (1,536 bytes) is enough for the longest standard Ethernet frame
             * (1522 bytes including FCS) plus packet header (4 bytes).
             */
            if (free_pages < 6)
            {
                rc = VERR_NET_NO_BUFFER_SPACE;
                Log2Func(("#%d: Buffer space low, returning %Rrc!\n", pThis->iInstance, rc));
            }
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnWaitReceiveAvail}
 */
static DECLCALLBACK(int) dpNicNet_WaitReceiveAvail(PPDMINETWORKDOWN pInterface, RTMSINTERVAL cMillies)
{
    PDPNICSTATECC   pThisCC = RT_FROM_MEMBER(pInterface, DPNICSTATECC, INetworkDown);
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);

    int rc = dp8390CanReceive(pDevIns, pThis);
    if (RT_SUCCESS(rc))
    {
        STAM_COUNTER_INC(&pThis->StatRxCanReceiveNow);
        return VINF_SUCCESS;
    }
    if (RT_UNLIKELY(cMillies == 0))
    {
        STAM_COUNTER_INC(&pThis->StatRxCannotReceiveNow);
        return VINF_SUCCESS; //VERR_NET_NO_BUFFER_SPACE;
    }

    rc = VERR_INTERRUPTED;
    ASMAtomicXchgBool(&pThis->fMaybeOutOfSpace, true);
    STAM_PROFILE_START(&pThis->StatRxOverflow, a);
    VMSTATE enmVMState;
    while (RT_LIKELY(   (enmVMState = PDMDevHlpVMState(pDevIns)) == VMSTATE_RUNNING
                     || enmVMState == VMSTATE_RUNNING_LS))
    {
        int rc2 = dp8390CanReceive(pDevIns, pThis);
        if (RT_SUCCESS(rc2))
        {
            rc = VINF_SUCCESS;
            break;
        }
        if (cMillies > 666)
            cMillies = 666;
        LogFlowFunc(("Waiting cMillies=%u...\n", cMillies));

        rc2 = RTSemEventWait(pThis->hEventOutOfRxSpace, cMillies);
//LogRelFunc(("RTSemEventWait: rc=%Rrc\n", rc2));
//        if (rc2 == VERR_TIMEOUT)
//            break;
    }
    STAM_PROFILE_STOP(&pThis->StatRxOverflow, a);
    ASMAtomicXchgBool(&pThis->fMaybeOutOfSpace, false);

    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnReceive}
 */
static DECLCALLBACK(int) dpNicNet_Receive(PPDMINETWORKDOWN pInterface, const void *pvBuf, size_t cb)
{
    PDPNICSTATECC   pThisCC = RT_FROM_MEMBER(pInterface, DPNICSTATECC, INetworkDown);
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    int             rc;

    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);
    rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertReleaseRC(rc);

    if (cb > 50) /* unqualified guess */
        pThis->Led.Asserted.s.fReading = pThis->Led.Actual.s.fReading = 1;
    dp8390CoreReceiveLocked(pDevIns, pThis, (const uint8_t *)pvBuf, cb);
    pThis->Led.Actual.s.fReading = 0;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnXmitPending}
 */
static DECLCALLBACK(void) dpNicNet_XmitPending(PPDMINETWORKDOWN pInterface)
{
    PDPNICSTATECC   pThisCC = RT_FROM_MEMBER(pInterface, DPNICSTATECC, INetworkDown);
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    dp8390CoreXmitPacket(pDevIns, pThis, true /*fOnWorkerThread*/);
}


/* -=-=-=-=-=- DPNICSTATE::INetworkConfig -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetMac}
 */
static DECLCALLBACK(int) dpNicGetMac(PPDMINETWORKCONFIG pInterface, PRTMAC pMac)
{
    PDPNICSTATECC   pThisCC = RT_FROM_MEMBER(pInterface, DPNICSTATECC, INetworkConfig);
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);

    LogFlowFunc(("#%d\n", pThis->iInstance));
    /// @todo This is broken!! We can't properly get the MAC address set by the guest
#if 0
    memcpy(pMac, pThis->core.pg1.PAR, sizeof(*pMac));
#else
    memcpy(pMac, pThis->aPROM, sizeof(*pMac));
#endif
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetLinkState}
 */
static DECLCALLBACK(PDMNETWORKLINKSTATE) dpNicGetLinkState(PPDMINETWORKCONFIG pInterface)
{
    PDPNICSTATECC   pThisCC = RT_FROM_MEMBER(pInterface, DPNICSTATECC, INetworkConfig);
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);

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
static DECLCALLBACK(int) dpNicSetLinkState(PPDMINETWORKCONFIG pInterface, PDMNETWORKLINKSTATE enmState)
{
    PDPNICSTATECC   pThisCC = RT_FROM_MEMBER(pInterface, DPNICSTATECC, INetworkConfig);
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    bool            fLinkUp;

    LogFlowFunc(("#%d\n", pThis->iInstance));
    AssertMsgReturn(enmState > PDMNETWORKLINKSTATE_INVALID && enmState <= PDMNETWORKLINKSTATE_DOWN_RESUME,
                    ("Invalid link state: enmState=%d\n", enmState), VERR_INVALID_PARAMETER);

    if (enmState == PDMNETWORKLINKSTATE_DOWN_RESUME)
    {
        dp8390TempLinkDown(pDevIns, pThis);
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
            pThis->cLinkRestorePostponed = 0;
            pThis->Led.Asserted.s.fError = pThis->Led.Actual.s.fError = 1;
            int rc = PDMDevHlpTimerSetMillies(pDevIns, pThis->hTimerRestore, pThis->cMsLinkUpDelay);
            AssertRC(rc);
        }
        else
        {
            /* Disconnect. */
            pThis->cLinkDownReported = 0;
            pThis->cLinkRestorePostponed = 0;
            pThis->Led.Asserted.s.fError = pThis->Led.Actual.s.fError = 1;
        }
        Assert(!PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
        if (pThisCC->pDrv)
            pThisCC->pDrv->pfnNotifyLinkChanged(pThisCC->pDrv, enmState);
    }
    return VINF_SUCCESS;
}


/* -=-=-=-=-=- DPNICSTATE::ILeds (LUN#0) -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMILEDPORTS,pfnQueryStatusLed}
 */
static DECLCALLBACK(int) dpNicQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PDPNICSTATECC   pThisCC = RT_FROM_MEMBER(pInterface, DPNICSTATECC, ILeds);
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    if (iLUN == 0)
    {
        *ppLed = &pThis->Led;
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/* -=-=-=-=-=- DPNICSTATE::IBase (LUN#0) -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) dpNicQueryInterface(struct PDMIBASE *pInterface, const char *pszIID)
{
    PDPNICSTATECC pThisCC = RT_FROM_MEMBER(pInterface, DPNICSTATECC, IBase);
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
static DECLCALLBACK(void) dpNicR3PowerOff(PPDMDEVINS pDevIns)
{
    /* Poke thread waiting for buffer space. */
    dp8390R3WakeupReceive(pDevIns);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDetach}
 *
 * One port on the network card has been disconnected from the network.
 */
static DECLCALLBACK(void) dpNicR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    PDPNICSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDPNICSTATECC);
    RT_NOREF(fFlags);
    LogFlowFunc(("#%d\n", pThis->iInstance));

    AssertLogRelReturnVoid(iLUN == 0);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    /*
     * Zero some important members.
     */
    pThis->fDriverAttached = false;
    pThisCC->pDrvBase = NULL;
    pThisCC->pDrv     = NULL;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnAttach}
 * One port on the network card has been connected to a network.
 */
static DECLCALLBACK(int) dpNicR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PDPNICSTATE      pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    PDPNICSTATECC    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDPNICSTATECC);
    RT_NOREF(fFlags);
    LogFlowFunc(("#%d\n", pThis->iInstance));

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
    }
    else if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
             || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        /* This should never happen because this function is not called
         * if there is no driver to attach! */
        LogFunc(("#%d No attached driver!\n", pThis->iInstance));
    }

    /*
     * Temporarily drop the link if it was up so that the guest
     * will know that we have changed the configuration of the
     * network card
     */
    if (RT_SUCCESS(rc))
        dp8390TempLinkDown(pDevIns, pThis);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnSuspend}
 */
static DECLCALLBACK(void) dpNicR3Suspend(PPDMDEVINS pDevIns)
{
    /* Poke thread waiting for buffer space. */
    dp8390R3WakeupReceive(pDevIns);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) dpNicR3Reset(PPDMDEVINS pDevIns)
{
    PDPNICSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    LogFlowFunc(("#%d\n", pThis->iInstance));
    if (pThis->fLinkTempDown)
    {
        pThis->cLinkDownReported = 0x1000;
        pThis->cLinkRestorePostponed = 0x1000;
        PDMDevHlpTimerStop(pDevIns, pThis->hTimerRestore);
        dpNicR3TimerRestore(pDevIns, pThis->hTimerRestore, pThis);
    }

    dpNicR3HardReset(pDevIns, pThis);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) dpNicR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    PDPNICSTATERC pThisRC = PDMINS_2_DATA_RC(pDevIns, PDPNICSTATERC);
    pThisRC->pDrv += offDelta;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) dpNicR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PDPNICSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);

    if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->CritSect))
    {
        RTSemEventSignal(pThis->hEventOutOfRxSpace);
        RTSemEventDestroy(pThis->hEventOutOfRxSpace);
        pThis->hEventOutOfRxSpace = NIL_RTSEMEVENT;
        PDMDevHlpCritSectDelete(pDevIns, &pThis->CritSect);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) dpNicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDPNICSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);
    PDPNICSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDPNICSTATECC);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    PPDMIBASE       pBase;
    char            szTmp[128];
    int             rc;

    /*
     * Init what's required to make the destructor safe.
     */
    pThis->iInstance            = iInstance;
    pThis->hEventOutOfRxSpace   = NIL_RTSEMEVENT;
    pThis->hIoPortsNic          = NIL_IOMIOPORTHANDLE;
    pThis->hIoPortsCore         = NIL_IOMIOPORTHANDLE;
    pThisCC->pDevIns            = pDevIns;

    /*
     * Validate configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "MAC|CableConnected|Port|MemBase|IRQ|DMA|DeviceType|LinkUpDelay|LineSpeed", "");

    /*
     * Read the configuration.
     */
    rc = pHlp->pfnCFGMQueryBytes(pCfg, "MAC", &pThis->MacConfigured, sizeof(pThis->MacConfigured));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"MAC\" value"));
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CableConnected", &pThis->fLinkUp, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"CableConnected\" value"));

    /*
     * Determine the model.
     */
    char szDeviceType[16];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "DeviceType", &szDeviceType[0], sizeof(szDeviceType), "NE2000");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"ChipType\" as string failed"));

    if (!strcmp(szDeviceType, "NE1000"))
        pThis->uDevType = DEV_NE1000;   /* Novell NE1000. */
    else if (!strcmp(szDeviceType, "NE2000"))
        pThis->uDevType = DEV_NE2000;   /* Novell NE2000. */
    else if (!strcmp(szDeviceType, "WD8003"))
        pThis->uDevType = DEV_WD8003;   /* WD EtherCard Plus. */
    else if (!strcmp(szDeviceType, "WD8013"))
        pThis->uDevType = DEV_WD8013;   /* WD EtherCard Plus 16. */
    else if (!strcmp(szDeviceType, "3C503"))
        pThis->uDevType = DEV_3C503;    /* 3Com 3C503 EtherLink II. */
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES, RT_SRC_POS,
                                   N_("Configuration error: The \"DeviceType\" value \"%s\" is unsupported"),
                                   szDeviceType);


    /*
     * Default resource assignments depend on the device type.
     */
    unsigned uDefIoPort  = 0;   /* To be overridden. */
    unsigned uDefIrq     = 0;
    unsigned uDefDma     = 0;   /* Default to no DMA. */
    unsigned uDefMemBase = 0;   /* Default to no shared memory. */

    if ((pThis->uDevType == DEV_NE1000) || (pThis->uDevType == DEV_NE2000))
    {
        uDefIoPort = 0x300;
        uDefIrq    = 3;
    }
    else if ((pThis->uDevType == DEV_WD8003) || (pThis->uDevType == DEV_WD8013))
    {
        uDefIoPort  = 0x280;
        uDefIrq     = 3;
        uDefMemBase = 0xd0000;
        pThis->cbMemSize = _8K;
        if (pThis->uDevType == DEV_WD8013)
            pThis->cbMemSize = _16K;
    }
    else if (pThis->uDevType == DEV_3C503)
    {
        uDefIoPort  = 0x300;
        uDefIrq     = 3;
        uDefDma     = 1;
        uDefMemBase = 0xdc000;
        pThis->cbMemSize = _8K;
    }

    /*
     * Process ISA configuration options.
     */
    rc = pHlp->pfnCFGMQueryPortDef(pCfg, "Port", &pThis->IOPortBase, uDefIoPort);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"Port\" value"));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "IRQ", &pThis->uIsaIrq, uDefIrq);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"IRQ\" value"));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "DMA", &pThis->uIsaDma, uDefDma);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"DMA\" value"));

    rc = pHlp->pfnCFGMQueryGCPtrDef(pCfg, "MemBase", &pThis->MemBase, uDefMemBase);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"MemBase\" value"));



    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "LinkUpDelay", (uint32_t*)&pThis->cMsLinkUpDelay, 5000); /* ms */
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the value of 'LinkUpDelay'"));
    Assert(pThis->cMsLinkUpDelay <= 300000); /* less than 5 minutes */
    if (pThis->cMsLinkUpDelay > 5000 || pThis->cMsLinkUpDelay < 100)
    {
        LogRel(("DPNIC#%d WARNING! Link up delay is set to %u seconds!\n",
                iInstance, pThis->cMsLinkUpDelay / 1000));
    }
    LogFunc(("#%d Link up delay is set to %u seconds\n",
         iInstance, pThis->cMsLinkUpDelay / 1000));


    /*
     * Initialize data (most of it anyway).
     */
    pThis->Led.u32Magic                       = PDMLED_MAGIC;
    /* IBase */
    pThisCC->IBase.pfnQueryInterface          = dpNicQueryInterface;
    /* INetworkPort */
    pThisCC->INetworkDown.pfnWaitReceiveAvail = dpNicNet_WaitReceiveAvail;
    pThisCC->INetworkDown.pfnReceive          = dpNicNet_Receive;
    pThisCC->INetworkDown.pfnXmitPending      = dpNicNet_XmitPending;
    /* INetworkConfig */
    pThisCC->INetworkConfig.pfnGetMac         = dpNicGetMac;
    pThisCC->INetworkConfig.pfnGetLinkState   = dpNicGetLinkState;
    pThisCC->INetworkConfig.pfnSetLinkState   = dpNicSetLinkState;
    /* ILeds */
    pThisCC->ILeds.pfnQueryStatusLed          = dpNicQueryStatusLed;

    pThis->hIoPortsCore = NIL_IOMIOPORTHANDLE;
    pThis->hIoPortsNic  = NIL_IOMIOPORTHANDLE;
    pThis->hSharedMem   = NIL_IOMMMIOHANDLE;

    /*
     * We use our own critical section (historical reasons).
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "DPNIC#%u", iInstance);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, &pThis->CritSect);
    AssertRCReturn(rc, rc);

    rc = RTSemEventCreate(&pThis->hEventOutOfRxSpace);
    AssertRCReturn(rc, rc);

    /*
     * Register ISA I/O ranges. This depends on the device type.
     */
    if ((pThis->uDevType == DEV_NE1000) || (pThis->uDevType == DEV_NE2000))
    {
        /* The NE1000 and NE2000 map the DP8390 at the beginning of the port range,
         * followed by the data/reset ports.
         */
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOPortBase, 0x10 /*cPorts*/, dp8390CoreIOPortWrite, dp8390CoreIOPortRead,
                                         "DP8390-Core", NULL /*paExtDesc*/, &pThis->hIoPortsCore);
        if (RT_FAILURE(rc))
            return rc;
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOPortBase + 0x10, 0x10 /*cPorts*/, neIOPortWrite, neIOPortRead,
                                         "DPNIC-NE", NULL /*paExtDesc*/, &pThis->hIoPortsNic);
        if (RT_FAILURE(rc))
            return rc;
    }
    else if ((pThis->uDevType == DEV_WD8003) || (pThis->uDevType == DEV_WD8013))
    {
        /* The WD8003 and WD8013 map the DP8390 at the end of the port range
         * (16 bytes into it). The first 8 bytes of the range are largely unused
         * while the second 8 bytes map the PROM.
         */
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOPortBase, 0x10 /*cPorts*/, wdIOPortWrite, wdIOPortRead,
                                         "DPNIC-WD", NULL /*paExtDesc*/, &pThis->hIoPortsNic);
        if (RT_FAILURE(rc))
            return rc;
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOPortBase + 0x10, 0x10 /*cPorts*/, dp8390CoreIOPortWrite, dp8390CoreIOPortRead,
                                         "DP8390-Core", NULL /*paExtDesc*/, &pThis->hIoPortsCore);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Shared memory MMIO area. This is rather lame.
         */
        rc = PDMDevHlpMmioCreateExAndMap(pDevIns, pThis->MemBase, pThis->cbMemSize,
                                         IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU | IOMMMIO_FLAGS_ABS,
                                         NULL /*pPciDev*/, UINT32_MAX /*iPciRegion*/,
                                         wdMemWrite, wdMemRead, NULL /*wdMmioFill*/, NULL /*pvUser*/,
                                         "DPNIC - WD Shared RAM", &pThis->hSharedMem);
        AssertRCReturn(rc, rc);

        /* Hack to make WD drivers happy. */
        memcpy(&pThis->MacConfigured, "\x00\x00\xC0", 3);
    }
    else if (pThis->uDevType == DEV_3C503)
    {
        /* The 3C503 maps the DP8390 at the base I/O address, except the first
         * or second 16 bytes of PROM can be mapped into the same space. The
         * custom Gate Array is mapped at I/O base + 400h.
         */
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOPortBase, 0x10 /*cPorts*/, dp8390CoreIOPortWrite, dp8390CoreIOPortRead,
                                         "DP8390-Core", NULL /*paExtDesc*/, &pThis->hIoPortsCore);
        if (RT_FAILURE(rc))
            return rc;

        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOPortBase + 0x400, 0x10 /*cPorts*/, elIOPortWrite, elIOPortRead,
                                         "DPNIC-EL", NULL /*paExtDesc*/, &pThis->hIoPortsNic);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Shared memory MMIO area. The same lame thing.
         */
        rc = PDMDevHlpMmioCreateExAndMap(pDevIns, pThis->MemBase, pThis->cbMemSize,
                                         IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU | IOMMMIO_FLAGS_ABS,
                                         NULL /*pPciDev*/, UINT32_MAX /*iPciRegion*/,
                                         elMemWrite, elMemRead, NULL /*elMmioFill*/, NULL /*pvUser*/,
                                         "DPNIC - 3C503 Shared RAM", &pThis->hSharedMem);
        AssertRCReturn(rc, rc);

        /*
         * Register DMA channel.
         */
        if ((pThis->uIsaDma >= ELNKII_MIN_VALID_DMA) && (pThis->uIsaDma <= ELNKII_MAX_VALID_DMA))
        {
            rc = PDMDevHlpDMARegister(pDevIns, pThis->uIsaDma, elnk3R3DMAXferHandler, pThis);
            if (RT_FAILURE(rc))
                return rc;
            LogRel(("DPNIC#%d: Enabling 3C503 DMA channel %u\n", iInstance, pThis->uIsaDma));
        }
        else
            LogRel(("DPNIC#%d: Disabling 3C503 DMA\n", iInstance));

        /* Hack to make 3C503 diagnostics happy. */
        memcpy(&pThis->MacConfigured, "\x02\x60\x8C", 3);
    }


    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, dpNicR3TimerRestore, NULL, TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_NO_RING0,
                              "DPNIC Link Restore Timer", &pThis->hTimerRestore);
    if (RT_FAILURE(rc))
        return rc;

    rc = PDMDevHlpSSMRegisterEx(pDevIns, DPNIC_SAVEDSTATE_VERSION, sizeof(*pThis), NULL,
                                NULL,           dpNicLiveExec, NULL,
                                dpNicSavePrep, dpNicSaveExec, NULL,
                                dpNicLoadPrep, dpNicLoadExec, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Create the transmit notifier signaller.
     */
    rc = PDMDevHlpTaskCreate(pDevIns, PDMTASK_F_RZ, "DPNIC-Xmit", dpNicR3XmitTaskCallback, NULL /*pvUser*/, &pThis->hXmitTask);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Create the RX notifier signaller.
     */
    rc = PDMDevHlpTaskCreate(pDevIns, PDMTASK_F_RZ, "DPNIC-Rcv", dpNicR3CanRxTaskCallback, NULL /*pvUser*/, &pThis->hCanRxTask);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register the info item.
     */
    RTStrPrintf(szTmp, sizeof(szTmp), "dpnic%d", pThis->iInstance);
    PDMDevHlpDBGFInfoRegister(pDevIns, szTmp, "dpnic info", dpNicR3Info);

    /*
     * Attach status driver (optional).
     */
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pBase, "Status Port");
    if (RT_SUCCESS(rc))
        pThisCC->pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
    else if (   rc != VERR_PDM_NO_ATTACHED_DRIVER
             && rc != VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        AssertMsgFailed(("Failed to attach to status driver. rc=%Rrc\n", rc));
        return rc;
    }

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
    }
    else if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
             || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        /* No error! */
        LogFunc(("No attached driver!\n"));
    }
    else
        return rc;

    /*
     * Reset the device state. (Do after attaching.)
     */
    dpNicR3HardReset(pDevIns, pThis);

    /*
     * Register statistics counters.
     */
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatReceiveBytes,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,          "Amount of data received",                "/Public/Net/DPNIC%u/BytesReceived", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatTransmitBytes,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,          "Amount of data transmitted",             "/Public/Net/DPNIC%u/BytesTransmitted", iInstance);

    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatReceiveBytes,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,          "Amount of data received",                "/Devices/DPNIC%d/ReceiveBytes", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatTransmitBytes,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,          "Amount of data transmitted",             "/Devices/DPNIC%d/TransmitBytes", iInstance);

#ifdef VBOX_WITH_STATISTICS
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatIOReadRZ,           STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling IO reads in RZ",               "/Devices/DPNIC%d/IO/ReadRZ", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatIOReadR3,           STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling IO reads in R3",               "/Devices/DPNIC%d/IO/ReadR3", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatIOWriteRZ,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling IO writes in RZ",              "/Devices/DPNIC%d/IO/WriteRZ", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatIOWriteR3,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling IO writes in R3",              "/Devices/DPNIC%d/IO/WriteR3", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatReceive,            STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling receive",                      "/Devices/DPNIC%d/Receive", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatRxOverflow,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_OCCURENCE, "Profiling RX overflows",            "/Devices/DPNIC%d/RxOverflow", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatRxOverflowWakeup,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES    , "Nr of RX overflow wakeups",              "/Devices/DPNIC%d/RxOverflowWakeup", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatRxCanReceiveNow,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES    , "Can receive immediately",                "/Devices/DPNIC%d/RxCanReceiveNow", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatRxCannotReceiveNow, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES    , "Cannot receive, not waiting",            "/Devices/DPNIC%d/RxCannotReceiveNow", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatTransmitRZ,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling transmits in RZ",              "/Devices/DPNIC%d/Transmit/TotalRZ", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatTransmitR3,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling transmits in R3",              "/Devices/DPNIC%d/Transmit/TotalR3", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatTransmitSendRZ,     STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling send transmit in RZ",          "/Devices/DPNIC%d/Transmit/SendRZ", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatTransmitSendR3,     STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling send transmit in R3",          "/Devices/DPNIC%d/Transmit/SendR3", iInstance);

    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatInterrupt,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling interrupt checks",             "/Devices/DPNIC%d/UpdateIRQ", iInstance);

    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatDropPktMonitor,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Dropped packet, monitor mode",           "/Devices/DPNIC%d/DropPktMonitor", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatDropPktRcvrDis,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Dropped packet, receiver not enabled",   "/Devices/DPNIC%d/DropPktRcvrDis", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatDropPktVeryShort,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Dropped packet less than 8 bytes long",  "/Devices/DPNIC%d/DropPktVeryShort", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatDropPktVMNotRunning,STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Dropped packet, VM not running",         "/Devices/DPNIC%d/DropPktVMNotRunning", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatDropPktNoLink,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Dropped packet, no link",                "/Devices/DPNIC%d/DropPktNoLink", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatDropPktNoMatch,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Dropped packet, address match reject",   "/Devices/DPNIC%d/DropPktNoMatch", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatDropPktNoBuffer,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Dropped packet, DP8390 buffer overflow", "/Devices/DPNIC%d/DropPktNoBuffer", iInstance);
#endif /* VBOX_WITH_STATISTICS */

    return VINF_SUCCESS;
}

#else

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) dpNicRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDPNICSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PDPNICSTATE);

    /* Critical section setup: */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, &pThis->CritSect);
    AssertRCReturn(rc, rc);

    /* NIC-specific ISA I/O ports: */
    if (pThis->hIoPortsNic != NIL_IOMIOPORTHANDLE)
    {
        switch (pThis->uDevType)
        {
        case DEV_NE1000:
        case DEV_NE2000:
            rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsNic, neIOPortWrite, neIOPortRead, NULL /*pvUser*/);
            AssertRCReturn(rc, rc);
            break;
        case DEV_WD8003:
        case DEV_WD8013:
            rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsNic, wdIOPortWrite, wdIOPortRead, NULL /*pvUser*/);
            AssertRCReturn(rc, rc);
            break;
        case DEV_3C503:
            rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsNic, elIOPortWrite, elIOPortRead, NULL /*pvUser*/);
            AssertRCReturn(rc, rc);
            break;
        default:
            /* Must not happen. */
            return VERR_INTERNAL_ERROR;
        }
    }

    /* Common DP8390 core I/O ports: */
    if (pThis->hIoPortsCore != NIL_IOMIOPORTHANDLE)
    {
        rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsCore, dp8390CoreIOPortWrite, dp8390CoreIOPortRead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
    }

    /* Shared RAM, if used: */
    if (pThis->hSharedMem != NIL_IOMMMIOHANDLE)
    {
        AssertRCReturn(rc, rc);
        switch (pThis->uDevType)
        {
        case DEV_WD8003:
        case DEV_WD8013:
            rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hSharedMem, wdMemWrite, wdMemRead, NULL /*pvUser*/);
            AssertRCReturn(rc, rc);
            break;
        case DEV_3C503:
            rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hSharedMem, elMemWrite, elMemRead, NULL /*pvUser*/);
            AssertRCReturn(rc, rc);
            break;
        case DEV_NE1000:
        case DEV_NE2000:
        default:
            /* Must not happen. */
            return VERR_INTERNAL_ERROR;
        }
    }

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceDP8390 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "dp8390",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_NETWORK,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DPNICSTATE),
    /* .cbInstanceCC = */           sizeof(DPNICSTATECC),
    /* .cbInstanceRC = */           sizeof(DPNICSTATERC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "National Semiconductor DP8390 based adapter.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           dpNicR3Construct,
    /* .pfnDestruct = */            dpNicR3Destruct,
    /* .pfnRelocate = */            dpNicR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               dpNicR3Reset,
    /* .pfnSuspend = */             dpNicR3Suspend,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              dpNicR3Attach,
    /* .pfnDetach = */              dpNicR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            dpNicR3PowerOff,
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
    /* .pfnConstruct = */           dpNicRZConstruct,
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
    /* .pfnConstruct = */           NULL,
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
