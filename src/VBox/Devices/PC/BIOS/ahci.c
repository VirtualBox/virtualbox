/* $Id: ahci.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * AHCI host adapter driver to boot from SATA disks.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include <stdint.h>
#include <string.h>
#include "biosint.h"
#include "ebda.h"
#include "inlines.h"
#include "pciutil.h"
#include "vds.h"

#if DEBUG_AHCI
# define DBG_AHCI(...)        BX_INFO(__VA_ARGS__)
#else
# define DBG_AHCI(...)
#endif

/* Number of S/G table entries in EDDS. */
#define NUM_EDDS_SG         17


/**
 * AHCI PRDT structure.
 */
typedef struct
{
    uint32_t    phys_addr;
    uint32_t    something;
    uint32_t    reserved;
    uint32_t    len;
} ahci_prdt;

/**
 * SATA D2H FIS (Device to Host Frame Information Structure).
 */
typedef struct {
    uint8_t     fis_type;   /* 34h */
    uint8_t     intr;       /* Bit 6 indicates interrupt status. */
    uint8_t     status;     /* Status register. */
    uint8_t     error;      /* Error register. */
    uint8_t     sec_no;     /* Sector number register. */
    uint8_t     cyl_lo;     /* Cylinder low register. */
    uint8_t     cyl_hi;     /* Cylinder high register. */
    uint8_t     dev_hd;     /* Device/head register. */
    uint8_t     sec_no_exp; /* Expanded sector number register. */
    uint8_t     cyl_lo_exp; /* Expanded cylinder low register. */
    uint8_t     cyl_hi_exp; /* Expanded cylinder high register. */
    uint8_t     resvd0;
    uint8_t     sec_cn;     /* Sector count register. */
    uint8_t     sec_cn_exp; /* Expanded sector count register. */
    uint16_t    resvd1;
    uint32_t    resvd2;
} fis_d2h;

ct_assert(sizeof(fis_d2h) == 20);

/**
 * AHCI controller data.
 */
typedef struct
{
    /** The AHCI command list as defined by chapter 4.2.2 of the Intel AHCI spec.
     *  Because the BIOS doesn't support NCQ only the first command header is defined
     *  to save memory. - Must be aligned on a 1K boundary.
     */
    uint32_t        aCmdHdr[0x8];
    /** Align the next structure on a 128 byte boundary. */
    uint8_t         abAlignment1[0x60];
    /** The command table of one request as defined by chapter 4.2.3 of the Intel AHCI spec.
     *  Must be aligned on 128 byte boundary.
     */
    uint8_t         abCmd[0x40];
    /** The ATAPI command region.
     *  Located 40h bytes after the beginning of the CFIS (Command FIS).
     */
    uint8_t         abAcmd[0x20];
    /** Align the PRDT structure on a 128 byte boundary. */
    uint8_t         abAlignment2[0x20];
    /** Physical Region Descriptor Table (PRDT) array. In other
     *  words, a scatter/gather descriptor list.
     */
    ahci_prdt       aPrdt[16];
    /** Memory for the received command FIS area as specified by chapter 4.2.1
     *  of the Intel AHCI spec. This area is normally 256 bytes big but to save memory
     *  only the first 96 bytes are used because it is assumed that the controller
     *  never writes to the UFIS or reserved area. - Must be aligned on a 256byte boundary.
     */
    uint8_t         abFisRecv[0x60];
    /** Base I/O port for the index/data register pair. */
    uint16_t        iobase;
    /** Current port which uses the memory to communicate with the controller. */
    uint8_t         cur_port;
    /** Current PRD index (for pre/post skip). */
    uint8_t         cur_prd;
    /** Saved high bits of EAX. */
    uint16_t        saved_eax_hi;
    /** VDS EDDS DMA buffer descriptor structure. */
    vds_edds        edds;
    vds_sg          edds_more_sg[NUM_EDDS_SG - 1];
} ahci_t;

/* The AHCI specific data must fit into 1KB (statically allocated). */
ct_assert(sizeof(ahci_t) <= 1024);

/** PCI configuration fields. */
#define PCI_CONFIG_CAP                  0x34

#define PCI_CAP_ID_SATACR               0x12
#define VBOX_AHCI_NO_DEVICE 0xffff

#define RT_BIT_32(bit) ((uint32_t)(1L << (bit)))

/** Global register set. */
#define AHCI_HBA_SIZE 0x100

/// @todo what are the casts good for?
#define AHCI_REG_CAP ((uint32_t)0x00)
#define AHCI_REG_GHC ((uint32_t)0x04)
# define AHCI_GHC_AE RT_BIT_32(31)
# define AHCI_GHC_IR RT_BIT_32(1)
# define AHCI_GHC_HR RT_BIT_32(0)
#define AHCI_REG_IS  ((uint32_t)0x08)
#define AHCI_REG_PI  ((uint32_t)0x0c)
#define AHCI_REG_VS  ((uint32_t)0x10)

/** Per port register set. */
#define AHCI_PORT_SIZE     0x80

#define AHCI_REG_PORT_CLB  0x00
#define AHCI_REG_PORT_CLBU 0x04
#define AHCI_REG_PORT_FB   0x08
#define AHCI_REG_PORT_FBU  0x0c
#define AHCI_REG_PORT_IS   0x10
# define AHCI_REG_PORT_IS_DHRS RT_BIT_32(0)
# define AHCI_REG_PORT_IS_TFES RT_BIT_32(30)
#define AHCI_REG_PORT_IE   0x14
#define AHCI_REG_PORT_CMD  0x18
# define AHCI_REG_PORT_CMD_ST  RT_BIT_32(0)
# define AHCI_REG_PORT_CMD_FRE RT_BIT_32(4)
# define AHCI_REG_PORT_CMD_FR  RT_BIT_32(14)
# define AHCI_REG_PORT_CMD_CR  RT_BIT_32(15)
#define AHCI_REG_PORT_TFD  0x20
#define AHCI_REG_PORT_SIG  0x24
#define AHCI_REG_PORT_SSTS 0x28
#define AHCI_REG_PORT_SCTL 0x2c
#define AHCI_REG_PORT_SERR 0x30
#define AHCI_REG_PORT_SACT 0x34
#define AHCI_REG_PORT_CI   0x38

/** Returns the absolute register offset from a given port and port register. */
#define AHCI_PORT_REG(port, reg)    (AHCI_HBA_SIZE + (port) * AHCI_PORT_SIZE + (reg))

#define AHCI_REG_IDX   0
#define AHCI_REG_DATA  4

/** Writes the given value to a AHCI register. */
#define AHCI_WRITE_REG(iobase, reg, val)    \
    outpd((iobase) + AHCI_REG_IDX, reg);    \
    outpd((iobase) + AHCI_REG_DATA, val)

/** Reads from a AHCI register. */
#define AHCI_READ_REG(iobase, reg, val)     \
    outpd((iobase) + AHCI_REG_IDX, reg);    \
    (val) = inpd((iobase) + AHCI_REG_DATA)

/** Writes to the given port register. */
#define VBOXAHCI_PORT_WRITE_REG(iobase, port, reg, val)     \
    AHCI_WRITE_REG((iobase), AHCI_PORT_REG((port), (reg)), val)

/** Reads from the given port register. */
#define VBOXAHCI_PORT_READ_REG(iobase, port, reg, val)      \
    AHCI_READ_REG((iobase), AHCI_PORT_REG((port), (reg)), val)

#define ATA_CMD_IDENTIFY_DEVICE     0xEC
#define ATA_CMD_IDENTIFY_PACKET     0xA1
#define ATA_CMD_PACKET              0xA0
#define AHCI_CMD_READ_DMA_EXT       0x25
#define AHCI_CMD_WRITE_DMA_EXT      0x35


/* Warning: Destroys high bits of EAX. */
uint32_t inpd(uint16_t port);
#pragma aux inpd =      \
    ".386"              \
    "in     eax, dx"    \
    "mov    dx, ax"     \
    "shr    eax, 16"    \
    "xchg   ax, dx"     \
    parm [dx] value [dx ax] modify nomemory;

/* Warning: Destroys high bits of EAX. */
void outpd(uint16_t port, uint32_t val);
#pragma aux outpd =     \
    ".386"              \
    "xchg   ax, cx"     \
    "shl    eax, 16"    \
    "mov    ax, cx"     \
    "out    dx, eax"    \
    parm [dx] [cx ax] modify nomemory;


/* Machinery to save/restore high bits of EAX. 32-bit port I/O needs to use
 * EAX, but saving/restoring EAX around each port access would be inefficient.
 * Instead, each externally callable routine must save the high bits before
 * modifying them and restore the high bits before exiting.
 */

/* Note: Reading high EAX bits destroys them - *must* be restored later. */
uint16_t eax_hi_rd(void);
#pragma aux eax_hi_rd = \
    ".386"              \
    "shr    eax, 16"    \
    value [ax] modify nomemory;

void eax_hi_wr(uint16_t);
#pragma aux eax_hi_wr = \
    ".386"              \
    "shl    eax, 16"    \
    parm [ax] modify nomemory;

void inline high_bits_save(ahci_t __far *ahci)
{
    ahci->saved_eax_hi = eax_hi_rd();
}

void inline high_bits_restore(ahci_t __far *ahci)
{
    eax_hi_wr(ahci->saved_eax_hi);
}

/**
 * Sets a given set of bits in a register.
 */
static void inline ahci_ctrl_set_bits(uint16_t iobase, uint16_t reg, uint32_t mask)
{
    outpd(iobase + AHCI_REG_IDX, reg);
    outpd(iobase + AHCI_REG_DATA, inpd(iobase + AHCI_REG_DATA) | mask);
}

/**
 * Clears a given set of bits in a register.
 */
static void inline ahci_ctrl_clear_bits(uint16_t iobase, uint16_t reg, uint32_t mask)
{
    outpd(iobase + AHCI_REG_IDX, reg);
    outpd(iobase + AHCI_REG_DATA, inpd(iobase + AHCI_REG_DATA) & ~mask);
}

/**
 * Returns whether at least one of the bits in the given mask is set
 * for a register.
 */
static uint8_t inline ahci_ctrl_is_bit_set(uint16_t iobase, uint16_t reg, uint32_t mask)
{
    outpd(iobase + AHCI_REG_IDX, reg);
    return (inpd(iobase + AHCI_REG_DATA) & mask) != 0;
}

/**
 * Extracts a range of bits from a register and shifts them
 * to the right.
 */
static uint16_t ahci_ctrl_extract_bits(uint32_t val, uint32_t mask, uint8_t shift)
{
    return (val & mask) >> shift;
}

/**
 * Converts a segment:offset pair into a 32bit physical address.
 */
static uint32_t ahci_addr_to_phys(void __far *ptr)
{
    return ((uint32_t)FP_SEG(ptr) << 4) + FP_OFF(ptr);
}

/**
 * Issues a command to the SATA controller and waits for completion.
 */
static void ahci_port_cmd_sync(ahci_t __far *ahci, uint8_t val)
{
    uint16_t        io_base;
    uint8_t         port;

    port    = ahci->cur_port;
    io_base = ahci->iobase;

    if (port != 0xff)
    {
        /* Prepare the command header. */
        ahci->aCmdHdr[0] = ((uint32_t)ahci->cur_prd << 16) | RT_BIT_32(7) | val;
        ahci->aCmdHdr[1] = 0;
        ahci->aCmdHdr[2] = ahci_addr_to_phys(&ahci->abCmd[0]);

        /* Enable Command and FIS receive engine. */
        ahci_ctrl_set_bits(io_base, AHCI_PORT_REG(port, AHCI_REG_PORT_CMD),
                           AHCI_REG_PORT_CMD_FRE | AHCI_REG_PORT_CMD_ST);

        /* Queue command. */
        VBOXAHCI_PORT_WRITE_REG(io_base, port, AHCI_REG_PORT_CI, 0x1);

        /* Wait for a D2H FIS. */
        DBG_AHCI("AHCI: Waiting for D2H FIS\n");
        while (ahci_ctrl_is_bit_set(io_base, AHCI_PORT_REG(port, AHCI_REG_PORT_IS),
                                    AHCI_REG_PORT_IS_DHRS | AHCI_REG_PORT_IS_TFES) == 0)
        {
            // This is where we'd need some kind of a yield functionality...
        }

        ahci_ctrl_set_bits(io_base, AHCI_PORT_REG(port, AHCI_REG_PORT_IS),
                           AHCI_REG_PORT_IS_DHRS); /* Acknowledge received D2H FIS. */

        /* Disable command engine. */
        ahci_ctrl_clear_bits(io_base, AHCI_PORT_REG(port, AHCI_REG_PORT_CMD),
                             AHCI_REG_PORT_CMD_ST);
        /* Caller must examine status. */
    }
    else
        DBG_AHCI("AHCI: Invalid port given\n");
}

/**
 * Issue command to device.
 */
static uint16_t ahci_cmd_data(bio_dsk_t __far *bios_dsk, uint8_t cmd)
{
    ahci_t __far    *ahci  = bios_dsk->ahci_seg :> 0;
    uint16_t        n_sect = bios_dsk->drqp.nsect;
    uint16_t        sectsz = bios_dsk->drqp.sect_sz;
    fis_d2h __far   *d2h;
    int             i;

    DBG_AHCI("AHCI: Data block at %04X:%04X\n", FP_SEG(ahci), FP_OFF(ahci));
    _fmemset(&ahci->abCmd[0], 0, sizeof(ahci->abCmd));

    /* Prepare the FIS. */
    ahci->abCmd[0]  = 0x27;         /* FIS type H2D. */
    ahci->abCmd[1]  = 1 << 7;       /* Command update. */
    ahci->abCmd[2]  = cmd;
    ahci->abCmd[3]  = 0;

    ahci->abCmd[4]  = bios_dsk->drqp.lba & 0xff;
    ahci->abCmd[5]  = (bios_dsk->drqp.lba >> 8) & 0xff;
    ahci->abCmd[6]  = (bios_dsk->drqp.lba >> 16) & 0xff;
    ahci->abCmd[7]  = RT_BIT_32(6); /* LBA access. */

    ahci->abCmd[8]  = (bios_dsk->drqp.lba >> 24) & 0xff;
    ahci->abCmd[9]  = (bios_dsk->drqp.lba >> 32) & 0xff;
    ahci->abCmd[10] = (bios_dsk->drqp.lba >> 40) & 0xff;
    ahci->abCmd[11] = 0;

    ahci->abCmd[12] = (uint8_t)(n_sect & 0xff);
    ahci->abCmd[13] = (uint8_t)((n_sect >> 8) & 0xff);

    /* Lock memory needed for DMA. */
    ahci->edds.num_avail = NUM_EDDS_SG;
    DBG_AHCI("AHCI: S/G list for %lu bytes\n", (uint32_t)n_sect * sectsz);
    vds_build_sg_list(&ahci->edds, bios_dsk->drqp.buffer, (uint32_t)n_sect * sectsz);

    /* Set up the PRDT. */
    for (i = 0; i < ahci->edds.num_used; ++i)
    {
        ahci->aPrdt[ahci->cur_prd].len       = ahci->edds.u.sg[i].size - 1;
        ahci->aPrdt[ahci->cur_prd].phys_addr = ahci->edds.u.sg[i].phys_addr;
        ++ahci->cur_prd;
    }

#if DEBUG_AHCI
    {
        uint16_t     prdt_idx;

        for (prdt_idx = 0; prdt_idx < ahci->cur_prd; ++prdt_idx) {
            DBG_AHCI("S/G entry %u: %5lu bytes @ %08lX\n", prdt_idx,
                     ahci->aPrdt[prdt_idx].len + 1, ahci->aPrdt[prdt_idx].phys_addr);
        }
    }
#endif

    /* Build variable part of first command DWORD (reuses 'cmd'). */
    if (cmd == AHCI_CMD_WRITE_DMA_EXT)
        cmd = RT_BIT_32(6);     /* Indicate a write to device. */
    else if (cmd == ATA_CMD_PACKET) {
        cmd |= RT_BIT_32(5);    /* Indicate ATAPI command. */
        ahci->abCmd[3] |= 1;    /* DMA transfers. */
    } else
        cmd = 0;

    cmd |= 5;   /* Five DWORDs. */

    ahci_port_cmd_sync(ahci, cmd);

    /* Examine operation status. */
    d2h = (void __far *)&ahci->abFisRecv[0x40];
    DBG_AHCI("AHCI: ERR=%02x, STAT=%02x, SCNT=%02x\n", d2h->error, d2h->status, d2h->sec_cn);

    /* Unlock the buffer again. */
    vds_free_sg_list(&ahci->edds);
    return d2h->error ? 4 : 0;
}

/**
 * Deinits the curent active port.
 */
static void ahci_port_deinit_current(ahci_t __far *ahci)
{
    uint16_t    io_base;
    uint8_t     port;

    io_base = ahci->iobase;
    port    = ahci->cur_port;

    if (port != 0xff)
    {
        /* Put the port into an idle state. */
        ahci_ctrl_clear_bits(io_base, AHCI_PORT_REG(port, AHCI_REG_PORT_CMD),
                             AHCI_REG_PORT_CMD_FRE | AHCI_REG_PORT_CMD_ST);

        while (ahci_ctrl_is_bit_set(io_base, AHCI_PORT_REG(port, AHCI_REG_PORT_CMD),
                                    AHCI_REG_PORT_CMD_FRE | AHCI_REG_PORT_CMD_ST | AHCI_REG_PORT_CMD_FR | AHCI_REG_PORT_CMD_CR) == 1)
        {
            DBG_AHCI("AHCI: Waiting for the port to idle\n");
        }

        /*
         * Port idles, set up memory for commands and received FIS and program the
         * address registers.
         */
        /// @todo merge memsets?
        _fmemset(&ahci->aCmdHdr[0], 0, sizeof(ahci->aCmdHdr));
        _fmemset(&ahci->abCmd[0], 0, sizeof(ahci->abCmd));
        _fmemset(&ahci->abFisRecv[0], 0, sizeof(ahci->abFisRecv));

        VBOXAHCI_PORT_WRITE_REG(io_base, port, AHCI_REG_PORT_FB, 0);
        VBOXAHCI_PORT_WRITE_REG(io_base, port, AHCI_REG_PORT_FBU, 0);

        VBOXAHCI_PORT_WRITE_REG(io_base, port, AHCI_REG_PORT_CLB, 0);
        VBOXAHCI_PORT_WRITE_REG(io_base, port, AHCI_REG_PORT_CLBU, 0);

        /* Disable all interrupts. */
        VBOXAHCI_PORT_WRITE_REG(io_base, port, AHCI_REG_PORT_IE, 0);

        ahci->cur_port = 0xff;
    }
}

/**
 * Brings a port into a minimal state to make device detection possible
 * or to queue requests.
 */
static void ahci_port_init(ahci_t __far *ahci, uint8_t u8Port)
{
    /* Deinit any other port first. */
    ahci_port_deinit_current(ahci);

    /* Put the port into an idle state. */
    ahci_ctrl_clear_bits(ahci->iobase, AHCI_PORT_REG(u8Port, AHCI_REG_PORT_CMD),
                         AHCI_REG_PORT_CMD_FRE | AHCI_REG_PORT_CMD_ST);

    while (ahci_ctrl_is_bit_set(ahci->iobase, AHCI_PORT_REG(u8Port, AHCI_REG_PORT_CMD),
                                AHCI_REG_PORT_CMD_FRE | AHCI_REG_PORT_CMD_ST | AHCI_REG_PORT_CMD_FR | AHCI_REG_PORT_CMD_CR) == 1)
    {
        DBG_AHCI("AHCI: Waiting for the port to idle\n");
    }

    /*
     * Port idles, set up memory for commands and received FIS and program the
     * address registers.
     */
    /// @todo just one memset?
    _fmemset(&ahci->aCmdHdr[0], 0, sizeof(ahci->aCmdHdr));
    _fmemset(&ahci->abCmd[0], 0, sizeof(ahci->abCmd));
    _fmemset(&ahci->abFisRecv[0], 0, sizeof(ahci->abFisRecv));

    DBG_AHCI("AHCI: FIS receive area %lx from %x:%x\n",
             ahci_addr_to_phys(&ahci->abFisRecv), FP_SEG(&ahci->abFisRecv), FP_OFF(&ahci->abFisRecv));
    VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_FB, ahci_addr_to_phys(&ahci->abFisRecv));
    VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_FBU, 0);

    DBG_AHCI("AHCI: CMD list area %lx\n", ahci_addr_to_phys(&ahci->aCmdHdr));
    VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_CLB, ahci_addr_to_phys(&ahci->aCmdHdr));
    VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_CLBU, 0);

    /* Disable all interrupts. */
    VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_IE, 0);
    VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_IS, 0xffffffff);
    /* Clear all errors. */
    VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_SERR, 0xffffffff);

    ahci->cur_port = u8Port;
    ahci->cur_prd  = 0;
}

/**
 * Read sectors from an attached AHCI device.
 *
 * @returns status code.
 * @param   bios_dsk    Pointer to disk request packet (in the
 *                      EBDA).
 */
int ahci_read_sectors(bio_dsk_t __far *bios_dsk)
{
    uint16_t        device_id;
    uint16_t        rc;

    device_id = VBOX_GET_AHCI_DEVICE(bios_dsk->drqp.dev_id);
    if (device_id > BX_MAX_AHCI_DEVICES)
        BX_PANIC("%s: device_id out of range %d\n", __func__, device_id);

    DBG_AHCI("%s: %u sectors @ LBA 0x%llx, device %d, port %d\n", __func__,
             bios_dsk->drqp.nsect, bios_dsk->drqp.lba,
             device_id, bios_dsk->ahcidev[device_id].port);

    high_bits_save(bios_dsk->ahci_seg :> 0);
    ahci_port_init(bios_dsk->ahci_seg :> 0, bios_dsk->ahcidev[device_id].port);
    rc = ahci_cmd_data(bios_dsk, AHCI_CMD_READ_DMA_EXT);
    DBG_AHCI("%s: transferred %lu bytes\n", __func__, ((ahci_t __far *)(bios_dsk->ahci_seg :> 0))->aCmdHdr[1]);
    bios_dsk->drqp.trsfsectors = bios_dsk->drqp.nsect;
    high_bits_restore(bios_dsk->ahci_seg :> 0);
    return rc;
}

/**
 * Write sectors to an attached AHCI device.
 *
 * @returns status code.
 * @param   bios_dsk    Pointer to disk request packet (in the
 *                      EBDA).
 */
int ahci_write_sectors(bio_dsk_t __far *bios_dsk)
{
    uint16_t        device_id;
    uint16_t        rc;

    device_id = VBOX_GET_AHCI_DEVICE(bios_dsk->drqp.dev_id);
    if (device_id > BX_MAX_AHCI_DEVICES)
        BX_PANIC("%s: device_id out of range %d\n", __func__, device_id);

    DBG_AHCI("%s: %u sectors @ LBA 0x%llx, device %d, port %d\n", __func__,
             bios_dsk->drqp.nsect, bios_dsk->drqp.lba, device_id,
             bios_dsk->ahcidev[device_id].port);

    high_bits_save(bios_dsk->ahci_seg :> 0);
    ahci_port_init(bios_dsk->ahci_seg :> 0, bios_dsk->ahcidev[device_id].port);
    rc = ahci_cmd_data(bios_dsk, AHCI_CMD_WRITE_DMA_EXT);
    DBG_AHCI("%s: transferred %lu bytes\n", __func__, ((ahci_t __far *)(bios_dsk->ahci_seg :> 0))->aCmdHdr[1]);
    bios_dsk->drqp.trsfsectors = bios_dsk->drqp.nsect;
    high_bits_restore(bios_dsk->ahci_seg :> 0);
    return rc;
}

/// @todo move
#define ATA_DATA_NO      0x00
#define ATA_DATA_IN      0x01
#define ATA_DATA_OUT     0x02

uint16_t ahci_cmd_packet(uint16_t device_id, uint8_t cmdlen, char __far *cmdbuf,
                         uint32_t length, uint8_t inout, char __far *buffer)
{
    bio_dsk_t __far *bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;
    ahci_t __far    *ahci;

    /* Data out is currently not supported. */
    if (inout == ATA_DATA_OUT) {
        BX_INFO("%s: DATA_OUT not supported yet\n", __func__);
        return 1;
    }

    /* Convert to AHCI specific device number. */
    device_id = VBOX_GET_AHCI_DEVICE(device_id);

    DBG_AHCI("%s: reading %lu bytes, device %d, port %d\n", __func__,
             length, device_id, bios_dsk->ahcidev[device_id].port);
    DBG_AHCI("%s: reading %u %u-byte sectors\n", __func__,
             bios_dsk->drqp.nsect, bios_dsk->drqp.sect_sz);

    bios_dsk->drqp.lba     = length << 8;     /// @todo xfer length limit
    bios_dsk->drqp.buffer  = buffer;
    bios_dsk->drqp.nsect   = length / bios_dsk->drqp.sect_sz;
//    bios_dsk->drqp.sect_sz = 2048;

    ahci = bios_dsk->ahci_seg :> 0;
    high_bits_save(ahci);

    ahci_port_init(bios_dsk->ahci_seg :> 0, bios_dsk->ahcidev[device_id].port);

    /* Copy the ATAPI command where the HBA can fetch it. */
    _fmemcpy(ahci->abAcmd, cmdbuf, cmdlen);

    /* Reset transferred counts. */
    /// @todo clear in calling code?
    bios_dsk->drqp.trsfsectors = 0;
    bios_dsk->drqp.trsfbytes   = 0;

    ahci_cmd_data(bios_dsk, ATA_CMD_PACKET);
    DBG_AHCI("%s: transferred %lu bytes\n", __func__, ahci->aCmdHdr[1]);
    bios_dsk->drqp.trsfbytes = ahci->aCmdHdr[1];
    high_bits_restore(ahci);

    return ahci->aCmdHdr[1] == 0 ? 4 : 0;
}

void ahci_port_detect_device(ahci_t __far *ahci, uint8_t u8Port)
{
    uint32_t                val;
    bio_dsk_t __far         *bios_dsk;
    volatile uint32_t __far *ticks;
    uint32_t                end_tick;
    int                     device_found = 0;

    ahci_port_init(ahci, u8Port);

    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;

    /* Reset connection. */
    VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_SCTL, 0x01);
    /*
     * According to the spec we should wait at least 1msec until the reset
     * is cleared but this is a virtual controller so we don't have to.
     */
    VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_SCTL, 0);

    /*
     * We do however have to wait for the device to initialize (the port reset
     * to complete). That can take up to 10ms according to the SATA spec (device
     * must send COMINIT within 10ms of COMRESET). We should be generous with
     * the wait because in the typical case there are no ports without a device
     * attached.
     */
    ticks = MK_FP( 0x40, 0x6C );
    end_tick = *ticks + 3;  /* Wait up to five BIOS ticks, something in 150ms range. */

    while( *ticks < end_tick )
    {
        /* If PxSSTS.DET is 3, everything went fine. */
        VBOXAHCI_PORT_READ_REG(ahci->iobase, u8Port, AHCI_REG_PORT_SSTS, val);
        if (ahci_ctrl_extract_bits(val, 0xfL, 0) == 3) {
            device_found = 1;
            break;
        }
    }

    /* Timed out, no device detected. */
    if (!device_found) {
        DBG_AHCI("AHCI: Timed out, no device detected on port %d\n", u8Port);
        return;
    }

    if (ahci_ctrl_extract_bits(val, 0xfL, 0) == 0x3)
    {
        uint8_t     abBuffer[0x0200];
        uint8_t     hdcount, devcount_ahci, hd_index;
        uint8_t     cdcount;
        uint8_t     removable;

        /* Clear all errors after the reset. */
        VBOXAHCI_PORT_WRITE_REG(ahci->iobase, u8Port, AHCI_REG_PORT_SERR, 0xffffffff);

        devcount_ahci = bios_dsk->ahci_devcnt;

        DBG_AHCI("AHCI: Device detected on port %d\n", u8Port);

        /// @todo Merge common HD/CDROM detection code
        if (devcount_ahci < BX_MAX_AHCI_DEVICES)
        {
            /* Device detected, enable FIS receive. */
            ahci_ctrl_set_bits(ahci->iobase, AHCI_PORT_REG(u8Port, AHCI_REG_PORT_CMD),
                               AHCI_REG_PORT_CMD_FRE);

            /* Check signature to determine device type. */
            VBOXAHCI_PORT_READ_REG(ahci->iobase, u8Port, AHCI_REG_PORT_SIG, val);
            if (val == 0x101)
            {
                uint64_t    sectors;
                uint16_t    cylinders, heads, spt;
                chs_t       lgeo;
                uint8_t     idxCmosChsBase;

                DBG_AHCI("AHCI: Detected hard disk\n");

                /* Identify device. */
                bios_dsk->drqp.lba     = 0;
                bios_dsk->drqp.buffer  = &abBuffer;
                bios_dsk->drqp.nsect   = 1;
                bios_dsk->drqp.sect_sz = 512;
                ahci_cmd_data(bios_dsk, ATA_CMD_IDENTIFY_DEVICE);

                /* Calculate index into the generic device table. */
                hd_index = devcount_ahci + BX_MAX_ATA_DEVICES + BX_MAX_SCSI_DEVICES;

                removable = *(abBuffer+0) & 0x80 ? 1 : 0;
                cylinders = *(uint16_t *)(abBuffer+(1*2));  // word 1
                heads     = *(uint16_t *)(abBuffer+(3*2));  // word 3
                spt       = *(uint16_t *)(abBuffer+(6*2));  // word 6
                sectors   = *(uint32_t *)(abBuffer+(60*2)); // word 60 and word 61

                if (sectors == 0x0FFFFFFF)  /* For disks bigger than ~128GB */
                    sectors = *(uint64_t *)(abBuffer+(100*2)); // words 100 to 103

                DBG_AHCI("AHCI: 0x%llx sectors\n", sectors);

                bios_dsk->ahcidev[devcount_ahci].port = u8Port;
                bios_dsk->devices[hd_index].type        = DSK_TYPE_AHCI;
                bios_dsk->devices[hd_index].device      = DSK_DEVICE_HD;
                bios_dsk->devices[hd_index].removable   = removable;
                bios_dsk->devices[hd_index].lock        = 0;
                bios_dsk->devices[hd_index].blksize     = 512;
                bios_dsk->devices[hd_index].translation = GEO_TRANSLATION_LBA;
                bios_dsk->devices[hd_index].sectors     = sectors;

                bios_dsk->devices[hd_index].pchs.heads     = heads;
                bios_dsk->devices[hd_index].pchs.cylinders = cylinders;
                bios_dsk->devices[hd_index].pchs.spt       = spt;

                /* Get logical CHS geometry. */
                switch (devcount_ahci)
                {
                    case 0:
                        idxCmosChsBase = 0x40;
                        break;
                    case 1:
                        idxCmosChsBase = 0x48;
                        break;
                    case 2:
                        idxCmosChsBase = 0x50;
                        break;
                    case 3:
                        idxCmosChsBase = 0x58;
                        break;
                    default:
                        idxCmosChsBase = 0;
                }
                if (idxCmosChsBase && inb_cmos(idxCmosChsBase+7))
                {
                    lgeo.cylinders = get_cmos_word(idxCmosChsBase /*, idxCmosChsBase+1*/);
                    lgeo.heads     = inb_cmos(idxCmosChsBase + 2);
                    lgeo.spt       = inb_cmos(idxCmosChsBase + 7);
                }
                else
                    set_geom_lba(&lgeo, sectors);   /* Default EDD-style translated LBA geometry. */

                BX_INFO("AHCI %d-P#%d: PCHS=%u/%u/%u LCHS=%u/%u/%u 0x%llx sectors\n", devcount_ahci,
                        u8Port, cylinders, heads, spt, lgeo.cylinders, lgeo.heads, lgeo.spt,
                        sectors);

                bios_dsk->devices[hd_index].lchs = lgeo;

                /* Store the ID of the disk in the BIOS hdidmap. */
                hdcount = bios_dsk->hdcount;
                bios_dsk->hdidmap[hdcount] = devcount_ahci + BX_MAX_ATA_DEVICES + BX_MAX_SCSI_DEVICES;
                hdcount++;
                bios_dsk->hdcount = hdcount;

                /* Update hdcount in the BDA. */
                hdcount = read_byte(0x40, 0x75);
                hdcount++;
                write_byte(0x40, 0x75, hdcount);
            }
            else if (val == 0xeb140101)
            {
                DBG_AHCI("AHCI: Detected ATAPI device\n");

                /* Identify packet device. */
                bios_dsk->drqp.lba     = 0;
                bios_dsk->drqp.buffer  = &abBuffer;
                bios_dsk->drqp.nsect   = 1;
                bios_dsk->drqp.sect_sz = 512;
                ahci_cmd_data(bios_dsk, ATA_CMD_IDENTIFY_PACKET);

                /* Calculate index into the generic device table. */
                hd_index = devcount_ahci + BX_MAX_ATA_DEVICES + BX_MAX_SCSI_DEVICES;

                removable = *(abBuffer+0) & 0x80 ? 1 : 0;

                bios_dsk->ahcidev[devcount_ahci].port   = u8Port;
                bios_dsk->devices[hd_index].type        = DSK_TYPE_AHCI;
                bios_dsk->devices[hd_index].device      = DSK_DEVICE_CDROM;
                bios_dsk->devices[hd_index].removable   = removable;
                bios_dsk->devices[hd_index].blksize     = 2048;
                bios_dsk->devices[hd_index].translation = GEO_TRANSLATION_NONE;

                /* Store the ID of the device in the BIOS cdidmap. */
                cdcount = bios_dsk->cdcount;
                bios_dsk->cdidmap[cdcount] = devcount_ahci + BX_MAX_ATA_DEVICES + BX_MAX_SCSI_DEVICES;
                cdcount++;
                bios_dsk->cdcount = cdcount;
            }
            else
                DBG_AHCI("AHCI: Ignoring unknown device\n");

            devcount_ahci++;
            bios_dsk->ahci_devcnt = devcount_ahci;
        }
        else
            DBG_AHCI("AHCI: Reached maximum device count, skipping\n");
    }
}

/**
 * Initializes the AHCI HBA and detects attached devices.
 */
static int ahci_hba_init(uint16_t io_base)
{
    uint8_t             i, cPorts;
    uint32_t            val;
    uint16_t            ebda_seg;
    uint16_t            ahci_seg;
    bio_dsk_t __far     *bios_dsk;
    ahci_t __far        *ahci;

    /* Allocate 1K of base memory (this will move the EBDA).
     * NB: The AHCI memory block must be 1K aligned. If the EBDA
     * gets relocated, it will land on a paragraph aligned boundary;
     * therefore the memory is allocated outside of the EBDA.
     */
    ahci_seg = conv_mem_alloc(1/*KB*/, 0/*in_ebda*/);
    if (ahci_seg == 0)
    {
        DBG_AHCI("AHCI: Could not allocate 1K of memory\n");
        return 0;
    }

    ebda_seg = read_word(0x0040, 0x000E);
    bios_dsk = ebda_seg :> &EbdaData->bdisk;

    AHCI_READ_REG(io_base, AHCI_REG_VS, val);
    DBG_AHCI("AHCI: Controller version: 0x%x (major) 0x%x (minor)\n",
             ahci_ctrl_extract_bits(val, 0xffff0000, 16),
             ahci_ctrl_extract_bits(val, 0x0000ffff,  0));

    bios_dsk->ahci_seg    = ahci_seg;
    bios_dsk->ahci_devcnt = 0;

    DBG_AHCI("AHCI: ahci_seg=%04x, size=%04x, pointer at EBDA:%04x (EBDA size=%04x)\n",
             bios_dsk->ahci_seg, sizeof(ahci_t), (uint16_t)&EbdaData->bdisk.ahci_seg, sizeof(ebda_data_t));

    ahci = ahci_seg :> 0;
    ahci->cur_port = 0xff;
    ahci->iobase   = io_base;

    /* Reset the controller. */
    ahci_ctrl_set_bits(io_base, AHCI_REG_GHC, AHCI_GHC_HR);
    do
    {
        AHCI_READ_REG(io_base, AHCI_REG_GHC, val);
    } while ((val & AHCI_GHC_HR) != 0);

    AHCI_READ_REG(io_base, AHCI_REG_CAP, val);
    cPorts = ahci_ctrl_extract_bits(val, 0x1f, 0) + 1; /* Extract number of ports.*/

    DBG_AHCI("AHCI: HBA has %u ports\n", cPorts);

    /* Go through the ports. */
    i = 0;
    while (i < 32)
    {
        if (ahci_ctrl_is_bit_set(io_base, AHCI_REG_PI, RT_BIT_32(i)) != 0)
        {
            DBG_AHCI("AHCI: Port %u is present\n", i);
            ahci_port_detect_device(ahci_seg :> 0, i);
            cPorts--;
            if (cPorts == 0)
                break;
        }
        i++;
    }

    return 0;
}

/**
 * Init the AHCI driver and detect attached disks.
 */
void BIOSCALL ahci_init(void)
{
    uint16_t    busdevfn;

    busdevfn = pci_find_classcode(0x00010601);
    if (busdevfn != VBOX_AHCI_NO_DEVICE)
    {
        uint8_t     u8Bus, u8DevFn;
        uint8_t     u8PciCapOff;

        u8Bus = (busdevfn & 0xff00) >> 8;
        u8DevFn = busdevfn & 0x00ff;

        DBG_AHCI("AHCI HBA at Bus %u DevFn 0x%x (raw 0x%x)\n", u8Bus, u8DevFn, busdevfn);

        /* Examine the capability list and search for the Serial ATA Capability Register. */
        u8PciCapOff = pci_read_config_byte(u8Bus, u8DevFn, PCI_CONFIG_CAP);

        while (u8PciCapOff != 0)
        {
            uint8_t     u8PciCapId = pci_read_config_byte(u8Bus, u8DevFn, u8PciCapOff);

            DBG_AHCI("Capability ID 0x%x at 0x%x\n", u8PciCapId, u8PciCapOff);

            if (u8PciCapId == PCI_CAP_ID_SATACR)
                break;

            /* Go on to the next capability. */
            u8PciCapOff = pci_read_config_byte(u8Bus, u8DevFn, u8PciCapOff + 1);
        }

        if (u8PciCapOff != 0)
        {
            uint8_t     u8Rev;

            DBG_AHCI("AHCI HBA with SATA Capability register at 0x%x\n", u8PciCapOff);

            /* Advance to the stuff behind the id and next capability pointer. */
            u8PciCapOff += 2;

            u8Rev = pci_read_config_byte(u8Bus, u8DevFn, u8PciCapOff);
            if (u8Rev == 0x10)
            {
                /* Read the SATACR1 register and get the bar and offset of the index/data pair register. */
                uint8_t     u8Bar = 0x00;
                uint16_t    u16Off = 0x00;
                uint16_t    u16BarOff = pci_read_config_word(u8Bus, u8DevFn, u8PciCapOff + 2);

                DBG_AHCI("SATACR1: 0x%x\n", u16BarOff);

                switch (u16BarOff & 0xf)
                {
                    case 0x04:
                        u8Bar = 0x10;
                        break;
                    case 0x05:
                        u8Bar = 0x14;
                        break;
                    case 0x06:
                        u8Bar = 0x18;
                        break;
                    case 0x07:
                        u8Bar = 0x1c;
                        break;
                    case 0x08:
                        u8Bar = 0x20;
                        break;
                    case 0x09:
                        u8Bar = 0x24;
                        break;
                    case 0x0f:
                    default:
                        /* Reserved or unsupported. */
                        DBG_AHCI("BAR 0x%x unsupported\n", u16BarOff & 0xf);
                }

                /* Get the offset inside the BAR from bits 4:15. */
                u16Off = (u16BarOff >> 4) * 4;

                if (u8Bar != 0x00)
                {
                    uint32_t    u32Bar = pci_read_config_dword(u8Bus, u8DevFn, u8Bar);

                    DBG_AHCI("BAR at 0x%x : 0x%x\n", u8Bar, u32Bar);

                    if ((u32Bar & 0x01) != 0)
                    {
                        int         rc;
                        uint16_t    u16AhciIoBase = (u32Bar & 0xfff0) + u16Off;

                        /* Enable PCI memory, I/O, bus mastering access in command register. */
                        pci_write_config_word(u8Bus, u8DevFn, 4, 0x7);

                        DBG_AHCI("I/O base: 0x%x\n", u16AhciIoBase);
                        rc = ahci_hba_init(u16AhciIoBase);
                    }
                    else
                        DBG_AHCI("BAR is MMIO\n");
                }
            }
            else
                DBG_AHCI("Invalid revision 0x%x\n", u8Rev);
        }
        else
            DBG_AHCI("AHCI HBA with no usable Index/Data register pair!\n");
    }
    else
        DBG_AHCI("No AHCI HBA!\n");
}
