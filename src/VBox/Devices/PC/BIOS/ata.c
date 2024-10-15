/* $Id: ata.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PC BIOS - ATA disk support.
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
 *  ROM BIOS for use with Bochs/Plex86/QEMU emulation environment
 *
 *  Copyright (C) 2002  MandrakeSoft S.A.
 *
 *    MandrakeSoft S.A.
 *    43, rue d'Aboukir
 *    75002 Paris - France
 *    http://www.linux-mandrake.com/
 *    http://www.mandrakesoft.com/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

/*
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */


#include <stdint.h>
#include <stdarg.h>
#include "inlines.h"
#include "biosint.h"
#include "ebda.h"
#include "ata.h"
#include "pciutil.h"


#if DEBUG_ATA
#  define BX_DEBUG_ATA(...) BX_DEBUG(__VA_ARGS__)
#else
#  define BX_DEBUG_ATA(...)
#endif


// ---------------------------------------------------------------------------
// Start of ATA/ATAPI Driver
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : initialization
// ---------------------------------------------------------------------------
void BIOSCALL ata_init(void)
{
    uint8_t         channel, device;
    bio_dsk_t __far *bios_dsk;

    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;

    // Channels info init.
    for (channel=0; channel<BX_MAX_ATA_INTERFACES; channel++) {
        bios_dsk->channels[channel].iface   = ATA_IFACE_NONE;
        bios_dsk->channels[channel].iobase1 = 0x0;
        bios_dsk->channels[channel].iobase2 = 0x0;
        bios_dsk->channels[channel].irq     = 0;
    }

    // Devices info init.
    for (device=0; device<BX_MAX_ATA_DEVICES; device++) {
        bios_dsk->devices[device].type        = DSK_TYPE_NONE;
        bios_dsk->devices[device].device      = DSK_DEVICE_NONE;
        bios_dsk->devices[device].removable   = 0;
        bios_dsk->devices[device].lock        = 0;
        bios_dsk->devices[device].mode        = ATA_MODE_NONE;
        bios_dsk->devices[device].blksize     = 0x200;
        bios_dsk->devices[device].translation = GEO_TRANSLATION_NONE;
        bios_dsk->devices[device].lchs.heads     = 0;
        bios_dsk->devices[device].lchs.cylinders = 0;
        bios_dsk->devices[device].lchs.spt       = 0;
        bios_dsk->devices[device].pchs.heads     = 0;
        bios_dsk->devices[device].pchs.cylinders = 0;
        bios_dsk->devices[device].pchs.spt       = 0;
        bios_dsk->devices[device].sectors        = 0;
    }

    // hdidmap  and cdidmap init.
    for (device=0; device<BX_MAX_STORAGE_DEVICES; device++) {
        bios_dsk->hdidmap[device] = BX_MAX_STORAGE_DEVICES;
        bios_dsk->cdidmap[device] = BX_MAX_STORAGE_DEVICES;
    }

    bios_dsk->hdcount = 0;
    bios_dsk->cdcount = 0;
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : initialize device parameters (CHS geometry)
// ---------------------------------------------------------------------------
uint16_t ata_set_params(bio_dsk_t __far *bios_dsk, uint8_t device)
{
    uint16_t        iobase1, iobase2;
    uint8_t         lheads, lsectors;
    uint8_t         pheads, psectors;
    uint8_t         status;

    psectors = bios_dsk->devices[device].pchs.spt;
    pheads   = bios_dsk->devices[device].pchs.heads - 1;

    lsectors = bios_dsk->devices[device].lchs.spt;
    lheads   = bios_dsk->devices[device].lchs.heads - 1;

    if ((psectors == lsectors) && (pheads == lheads)) {
        // If LCHS == PCHS, we can save ourselves the bother
        // because INITIALIZE DEVICE PARAMETERS won't change anything.
        return 0;
    }

    iobase1 = bios_dsk->channels[device / 2].iobase1;
    iobase2 = bios_dsk->channels[device / 2].iobase2;

    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
    {
        BX_DEBUG_ATA("%s: disk busy\n", __func__);
        return 1;
    }

    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    outb(iobase1 + ATA_CB_FR, 0x00);
    outb(iobase1 + ATA_CB_SC, lsectors);
    outb(iobase1 + ATA_CB_DH, ((device & 1) ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0) | lheads );
    outb(iobase1 + ATA_CB_CMD, ATA_CMD_INITIALIZE_DEVICE_PARAMETERS);

    int_enable(); // enable higher priority interrupts

    while (1) {
        status = inb(iobase1 + ATA_CB_STAT);
        if ( !(status & ATA_CB_STAT_BSY) )
            break;
    }

    // Enable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
    return 0;
}


// ---------------------------------------------------------------------------
// ATA/ATAPI driver : software reset
// ---------------------------------------------------------------------------
// ATA-3
// 8.2.1 Software reset - Device 0
// NB: If two hard disks are installed on a single channel, *both* are reset
//     by setting the SRST bit.

void   ata_reset(uint16_t device)
{
    uint16_t        iobase1, iobase2;
    uint8_t         channel, sn, sc;
    uint16_t        max;
    uint16_t        pdelay;
    bio_dsk_t __far *bios_dsk;

    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;
    channel = device / 2;
    device  = channel * 2;

    iobase1 = bios_dsk->channels[channel].iobase1;
    iobase2 = bios_dsk->channels[channel].iobase2;

    // Reset

    // 8.2.1 (a) -- set SRST in DC
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN | ATA_CB_DC_SRST);

    // 8.2.1 (b) -- wait for BSY
    max=0xff;
    while(--max>0) {
        uint8_t status = inb(iobase1+ATA_CB_STAT);
        if ((status & ATA_CB_STAT_BSY) != 0)
            break;
    }

    // 8.2.1 (f) -- clear SRST
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);

    // 8.2.1 (h) -- wait for not BSY
    max=0xffff; /* The ATA specification says that the drive may be busy for up to 30 seconds. */
    while(--max>0) {
        uint8_t status = inb(iobase1+ATA_CB_STAT);
        if ((status & ATA_CB_STAT_BSY) == 0)
            break;
        pdelay=0xffff;
        while (--pdelay>0) {
            /* nothing */
        }
    }

    for ( ; (device & ~1) == (channel * 2); ++device) {
        if (bios_dsk->devices[device].type == DSK_TYPE_ATA) {
            // 8.2.1 (g) -- check for sc==sn==0x01
            // select device
            outb(iobase1+ATA_CB_DH, (device & 1) ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
            sc = inb(iobase1+ATA_CB_SC);
            sn = inb(iobase1+ATA_CB_SN);

            if ( (sc==0x01) && (sn==0x01) ) {
                // 8.2.1 (i) -- wait for DRDY
                max = 0x10; /* Speed up for virtual drives. Disks are immediately ready, CDs never */
                while(--max>0) {
                    uint8_t status = inb(iobase1+ATA_CB_STAT);
                    if ((status & ATA_CB_STAT_RDY) != 0)
                        break;
                }
                ata_set_params(bios_dsk, device);
            }
        }
    }

    // Enable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a data-in command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : BUSY bit set
      // 2 : read error
      // 3 : expected DRQ=1
      // 4 : no sectors left to read/verify
      // 5 : more sectors to read/verify
      // 6 : no sectors left to write
      // 7 : more sectors to write
uint16_t ata_cmd_data_in(bio_dsk_t __far *bios_dsk, uint16_t command, uint16_t count)
{
    uint16_t        iobase1, iobase2, blksize, mult_blk_cnt;
    uint16_t        cylinder;
    uint8_t         head;
    uint8_t         sector;
    uint8_t         device;
    uint8_t         status, mode;
    char __far      *buffer;

    device  = bios_dsk->drqp.dev_id;

    iobase1 = bios_dsk->channels[device / 2].iobase1;
    iobase2 = bios_dsk->channels[device / 2].iobase2;
    mode    = bios_dsk->devices[device].mode;
    blksize = bios_dsk->devices[device].blksize;
    if (blksize == 0) {   /* If transfer size is exactly 64K */
#if VBOX_BIOS_CPU >= 80386
        if (mode == ATA_MODE_PIO32)
            blksize = 0x4000;
        else
#endif
            blksize = 0x8000;
    } else {
#if VBOX_BIOS_CPU >= 80386
        if (mode == ATA_MODE_PIO32)
            blksize >>= 2;
        else
#endif
            blksize >>= 1;
    }

    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
    {
        BX_DEBUG_ATA("%s: disk busy\n", __func__);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 1;
    }

    buffer   = bios_dsk->drqp.buffer;
    sector   = bios_dsk->drqp.sector;
    cylinder = bios_dsk->drqp.cylinder;
    head     = bios_dsk->drqp.head;

    // sector will be 0 only on lba access. Convert to lba-chs
    if (sector == 0) {
        if (bios_dsk->drqp.lba + count >= 268435456)
        {
            sector = (bios_dsk->drqp.lba >> 24) & 0x00ff;
            cylinder = (bios_dsk->drqp.lba >> 32) & 0xffff;
            outb(iobase1 + ATA_CB_SC, (count & 0xff00) >> 8);
            outb(iobase1 + ATA_CB_SN, sector);
            outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
            outb(iobase1 + ATA_CB_CH, cylinder >> 8);
            /* Leave the bottom 24 bits as is, they are treated correctly by the
            * LBA28 code path. */
        }
        sector   = bios_dsk->drqp.lba & 0x000000ffL;
        cylinder = (bios_dsk->drqp.lba >> 8) & 0x0000ffffL;
        head     = ((bios_dsk->drqp.lba >> 24) & 0x0000000fL) | 0x40;
    }

    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    outb(iobase1 + ATA_CB_FR, 0x00);
    outb(iobase1 + ATA_CB_SC, count);
    outb(iobase1 + ATA_CB_SN, sector);
    outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
    outb(iobase1 + ATA_CB_CH, cylinder >> 8);
    outb(iobase1 + ATA_CB_DH, ((device & 1) ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0) | head );
    outb(iobase1 + ATA_CB_CMD, command);

    if (command == ATA_CMD_READ_MULTIPLE || command == ATA_CMD_READ_MULTIPLE_EXT) {
        mult_blk_cnt = count;
        count = 1;
    } else {
        mult_blk_cnt = 1;
    }

    while (1) {
        status = inb(iobase1 + ATA_CB_STAT);
        if ( !(status & ATA_CB_STAT_BSY) )
            break;
    }

    if (status & ATA_CB_STAT_ERR) {
        BX_DEBUG_ATA("%s: read error\n", __func__);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("%s: DRQ not set (status %02x)\n", __func__, (unsigned) status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 3;
    }

    // FIXME : move seg/off translation here

    int_enable(); // enable higher priority interrupts

    while (1) {

        // adjust if there will be an overrun. 2K max sector size
        if (FP_OFF(buffer) >= 0xF800)
            buffer = MK_FP(FP_SEG(buffer) + 0x80, FP_OFF(buffer) - 0x800);

#if VBOX_BIOS_CPU >= 80386
        if (mode == ATA_MODE_PIO32)
            buffer = rep_insd(buffer, blksize, iobase1);
        else
#endif
            buffer = rep_insw(buffer, blksize, iobase1);
        bios_dsk->drqp.trsfsectors += mult_blk_cnt;
        count--;
        while (1) {
            status = inb(iobase1 + ATA_CB_STAT);
            if ( !(status & ATA_CB_STAT_BSY) )
                break;
        }
        if (count == 0) {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
              != ATA_CB_STAT_RDY ) {
                BX_DEBUG_ATA("%s: no sectors left (status %02x)\n", __func__, (unsigned) status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 4;
            }
            break;
        }
        else {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
              != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                BX_DEBUG_ATA("%s: more sectors left (status %02x)\n", __func__, (unsigned) status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 5;
            }
            continue;
        }
    }
    // Enable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
    return 0;
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : device detection
// ---------------------------------------------------------------------------

int ata_signature(uint16_t iobase1, uint8_t channel, uint8_t slave)
{
    int         dsk_type = DSK_TYPE_NONE;
    uint8_t     sc, sn, st, cl, ch;

    /*
     * Wait for BSY=0 so that the signature can be read. We already determined that
     * an ATA interface is present, and rely on the fact that for non-existent
     * devices, the BSY bit will always be clear.
     */
    outb(iobase1+ATA_CB_DH, slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
    do {
        st = inb(iobase1+ATA_CB_STAT);
    } while (st & ATA_CB_STAT_BSY);

    /*
     * Look for ATA/ATAPI signature. Fun Fact #1: If there's a Device 1 but no
     * Device 0, Device 1 can't tell and does not respond for it. Accessing
     * non-existent Device 0 behaves the same regardless of whether Device 1
     * is present or not.
     */
    sc = inb(iobase1+ATA_CB_SC);
    sn = inb(iobase1+ATA_CB_SN);
    if ((sc == 1) && (sn == 1)) {
        cl = inb(iobase1+ATA_CB_CL);
        ch = inb(iobase1+ATA_CB_CH);

        /*
         * Fun fact #2: If Device 0 responds for Device 1, an ATA device generally
         * returns the values of its own registers, while an ATAPI device returns
         * zeros. In both cases, the Status register is read as zero.
         */
        if ((cl == 0x14) && (ch == 0xEB)) {
            dsk_type = DSK_TYPE_ATAPI;
            BX_DEBUG_ATA("ata%d-%d: ATAPI device\n", channel, slave);
        } else if ((cl == 0) && (ch == 0)) {
            if (st != 0) {
                dsk_type = DSK_TYPE_ATA;
                BX_DEBUG_ATA("ata%d-%d: ATA device\n", channel, slave);
            } else {
                BX_DEBUG_ATA("ata%d-%d: ATA master responding for slave\n", channel, slave);
            }
        } else {
            dsk_type = DSK_TYPE_UNKNOWN;
            BX_DEBUG_ATA("ata%d-%d: something else (%02X/%02X/%02X)\n", channel, slave, cl, ch, st);
        }
    } else  /* Possibly ATAPI Device 0 responding for Device 1. */
        BX_DEBUG_ATA("ata%d-%d: bad sc/sn signature (%02X/%02X)\n", channel, slave, sc, sn);

    return dsk_type;
}

void BIOSCALL ata_detect(void)
{
    uint16_t        ebda_seg = read_word(0x0040,0x000E);
    uint8_t         hdcount, cdcount, device, type;
    uint8_t         buffer[0x0200];
    bio_dsk_t __far *bios_dsk;

    /* If we have PCI support, look for an IDE controller (it has to be a PCI device)
     * so that we can skip silly probing. If there's no PCI, assume IDE is present.
     *
     * Needs an internal PCI function because the Programming Interface byte can be
     * almost anything, and we conly care about the base-class and sub-class code.
     */
#if VBOX_BIOS_CPU >= 80386
    uint16_t        busdevfn;

    busdevfn = pci_find_class_noif(0x0101);
    if (busdevfn == 0xffff) {
        BX_INFO("No PCI IDE controller, not probing IDE\n");
        return;
    }
#endif

    bios_dsk = ebda_seg :> &EbdaData->bdisk;

#if BX_MAX_ATA_INTERFACES > 0
    bios_dsk->channels[0].iface   = ATA_IFACE_ISA;
    bios_dsk->channels[0].iobase1 = 0x1f0;
    bios_dsk->channels[0].iobase2 = 0x3f0;
    bios_dsk->channels[0].irq     = 14;
#endif
#if BX_MAX_ATA_INTERFACES > 1
    bios_dsk->channels[1].iface   = ATA_IFACE_ISA;
    bios_dsk->channels[1].iobase1 = 0x170;
    bios_dsk->channels[1].iobase2 = 0x370;
    bios_dsk->channels[1].irq     = 15;
#endif
#if BX_MAX_ATA_INTERFACES > 2
    bios_dsk->channels[2].iface   = ATA_IFACE_ISA;
    bios_dsk->channels[2].iobase1 = 0x1e8;
    bios_dsk->channels[2].iobase2 = 0x3e0;
    bios_dsk->channels[2].irq     = 12;
#endif
#if BX_MAX_ATA_INTERFACES > 3
    bios_dsk->channels[3].iface   = ATA_IFACE_ISA;
    bios_dsk->channels[3].iobase1 = 0x168;
    bios_dsk->channels[3].iobase2 = 0x360;
    bios_dsk->channels[3].irq     = 11;
#endif
#if BX_MAX_ATA_INTERFACES > 4
#error Please fill the ATA interface information
#endif

    // Device detection
    hdcount = cdcount = 0;

    for (device = 0; device < BX_MAX_ATA_DEVICES; device++) {
        uint16_t    iobase1, iobase2;
        uint16_t    retries;
        uint8_t     channel, slave;
        uint8_t     st;

        channel = device / 2;
        slave = device % 2;

        iobase1 = bios_dsk->channels[channel].iobase1;
        iobase2 = bios_dsk->channels[channel].iobase2;

        /*
         * Here we are in a tricky situation. We do not know if an ATA
         * interface is even present at a given address. If it is present,
         * we don't know if a device is present. We also need to consider
         * the case of only a slave device being present, which does not
         * respond for the missing master device. If a device is present,
         * it may be still powering up or processing reset, which means it
         * may be busy.
         *
         * If a device is busy, we can't reliably write any registers, and
         * reads will return the Status register. If the Status register
         * value is 0FFh, there might be no ATA controller at all, or it
         * might be a busy drive. Fortunately we know that our own devices
         * never return such a value when busy, and we use that knowledge
         * to detect non-existent interfaces.
         *
         * We also know that our ATA interface will not return 0FFh even when
         * no device is present on a given channel. This knowledge is handy
         * when only a slave device exists because we won't read 0FFh and
         * think there is no ATA interface at all.
         */

        st = inb(iobase1+ATA_CB_STAT);
        BX_DEBUG_ATA("ata%d-%d: Status=%02X\n", channel, slave, st);
        if (st == 0xff)
            continue;

        /*
         * Perform a software reset by setting and clearing the SRST bit. This
         * can be done at any time, and forces device signature into the task file
         * registers. If present, both devices are reset at once, so we only do
         * this once per channel.
         */
        if (!slave) {
            outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN | ATA_CB_DC_SRST);

            /*
             * Ensure reasonable SRST pulse width, but do not wait long for
             * non-existent devices.
             */
            retries = 32;
            while (--retries > 0) {
                st = inb(iobase1+ATA_CB_STAT);
                if (st & ATA_CB_STAT_BSY)
                    break;
            }

            outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);

            /* After reset, device signature will be placed in registers. But
             * executing any commands will overwrite it for Device 1. So that
             * we don't have to reset twice, look for both Device 0 and Device 1
             * signatures here right after reset.
             */
            bios_dsk->devices[device + 0].type = ata_signature(iobase1, channel, 0);
            bios_dsk->devices[device + 1].type = ata_signature(iobase1, channel, 1);
        }

        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);

        type = bios_dsk->devices[device].type;

        // Now we send a IDENTIFY command to ATA device
        if (type == DSK_TYPE_ATA) {
            uint64_t    sectors;
            uint16_t    cylinders, heads, spt, blksize;
            chs_t       lgeo;
            uint8_t     chsgeo_base;
            uint8_t     removable, mode;

            //Temporary values to do the transfer
            bios_dsk->devices[device].device = DSK_DEVICE_HD;
            bios_dsk->devices[device].mode   = ATA_MODE_PIO16;
            bios_dsk->drqp.buffer = buffer;
            bios_dsk->drqp.dev_id = device;

            if (ata_cmd_data_in(bios_dsk, ATA_CMD_IDENTIFY_DEVICE, 1) !=0 )
                BX_PANIC("ata-detect: Failed to detect ATA device\n");

            removable = (*(buffer+0) & 0x80) ? 1 : 0;
#if VBOX_BIOS_CPU >= 80386
            mode      = *(buffer+96) ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
#else
            mode      = ATA_MODE_PIO16;
#endif
            blksize   = 512;  /* There is no sector size field any more. */

            cylinders = *(uint16_t *)(buffer+(1*2)); // word 1
            heads     = *(uint16_t *)(buffer+(3*2)); // word 3
            spt       = *(uint16_t *)(buffer+(6*2)); // word 6

            sectors   = *(uint32_t *)(buffer+(60*2)); // word 60 and word 61
            if (sectors == 0x0FFFFFFF)  /* For disks bigger than ~128GB */
                sectors = *(uint64_t *)(buffer+(100*2)); // words 100 to 103
            switch (device)
            {
            case 0:
                chsgeo_base = 0x1e;
                break;
            case 1:
                chsgeo_base = 0x26;
                break;
            case 2:
                chsgeo_base = 0x67;
                break;
            case 3:
                chsgeo_base = 0x70;
                break;
            default:
                chsgeo_base = 0;
            }
            if (chsgeo_base)
            {
                lgeo.cylinders = get_cmos_word(chsgeo_base /*, chsgeo_base + 1*/);
                lgeo.heads     = inb_cmos(chsgeo_base + 2);
                lgeo.spt       = inb_cmos(chsgeo_base + 7);
            }
            else
                set_geom_lba(&lgeo, sectors);   /* Default EDD-style translated LBA geometry. */

            BX_INFO("ata%d-%d: PCHS=%u/%u/%u LCHS=%u/%u/%u\n", channel, slave,
                    cylinders, heads, spt, lgeo.cylinders, lgeo.heads, lgeo.spt);

            bios_dsk->devices[device].device         = DSK_DEVICE_HD;
            bios_dsk->devices[device].removable      = removable;
            bios_dsk->devices[device].mode           = mode;
            bios_dsk->devices[device].blksize        = blksize;
            bios_dsk->devices[device].pchs.heads     = heads;
            bios_dsk->devices[device].pchs.cylinders = cylinders;
            bios_dsk->devices[device].pchs.spt       = spt;
            bios_dsk->devices[device].sectors        = sectors;
            bios_dsk->devices[device].lchs           = lgeo;
            if (device < 2)
            {
                uint8_t             sum, i;
                fdpt_t __far        *fdpt;
                void __far * __far  *int_vec;

                if (device == 0)
                    fdpt = ebda_seg :> &EbdaData->fdpt0;
                else
                    fdpt = ebda_seg :> &EbdaData->fdpt1;

                /* Set the INT 41h or 46h pointer. */
                int_vec  = MK_FP(0, (0x41 + device * 5) * sizeof(void __far *));
                *int_vec = fdpt;

                /* Update the DPT for drive 0/1 pointed to by Int41/46. This used
                 * to be done at POST time with lots of ugly assembler code, which
                 * isn't worth the effort of converting from AMI to Award CMOS
                 * format. Just do it here. */
                fdpt->resvd1 = fdpt->resvd2 = 0;

                fdpt->lcyl  = lgeo.cylinders;
                fdpt->lhead = lgeo.heads;
                fdpt->sig   = 0xa0;
                fdpt->spt   = spt;
                fdpt->cyl   = cylinders;
                fdpt->head  = heads;
                fdpt->lspt  = lgeo.spt;
                sum = 0;
                for (i = 0; i < 0xf; i++)
                    sum += *((uint8_t __far *)fdpt + i);
                sum = -sum;
                fdpt->csum = sum;

                /* Read the drive type from the CMOS. If it is one of the old
                 * IBM compatible drive types (rather than Type 47 or so), point
                 * INT 41h/46h at the drive table in ROM.
                 * This is required for some old guests which look at INT 41h/46h,
                 * but either insist that it points well above 640K (NetWare 2.x)
                 * or wipe out all RAM (286 XENIX 2.1.3/2.2.1).
                 *
                 * NB: Writing into the F000 segment and storing the FDPT there
                 * would also solve some of these problems.
                 */
                i   = inb_cmos(0x12);
                i >>= ((1 - device) * 4);
                i  &= 0x0f;
                if (i == 0xf)
                    i = inb_cmos(0x19 + device);

                if (i <= 23) {  // Should be in sync with DevPcBios.cpp and orgs.asm
                    fdpt = MK_FP(0xF000, 0xE401);
                    fdpt += i - 1;
                    *int_vec = fdpt;
                }
            }

            // Set the right CHS geometry if needed
            ata_set_params(bios_dsk, device);

            // fill hdidmap
            bios_dsk->hdidmap[hdcount] = device;
            hdcount++;
        }

        // Now we send an IDENTIFY command to ATAPI device
        if (type == DSK_TYPE_ATAPI) {
            uint8_t     type, removable, mode;
            uint16_t    blksize;

            // Temporary values to do the transfer
            bios_dsk->devices[device].device = DSK_DEVICE_CDROM;
            bios_dsk->devices[device].mode   = ATA_MODE_PIO16;
            bios_dsk->drqp.buffer = buffer;
            bios_dsk->drqp.dev_id = device;

            if (ata_cmd_data_in(bios_dsk, ATA_CMD_IDENTIFY_PACKET, 1) != 0)
                BX_PANIC("ata-detect: Failed to detect ATAPI device\n");

            type      = *(buffer+1) & 0x1f;
            removable = (*(buffer+0) & 0x80) ? 1 : 0;
#if VBOX_BIOS_CPU >= 80386
            mode      = *(buffer+96) ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
#else
            mode      = ATA_MODE_PIO16;
#endif
            blksize   = 2048;

            bios_dsk->devices[device].device    = type;
            bios_dsk->devices[device].removable = removable;
            bios_dsk->devices[device].mode      = mode;
            bios_dsk->devices[device].blksize   = blksize;

            // fill cdidmap
            bios_dsk->cdidmap[cdcount] = device;
            cdcount++;
        }

        {
            uint32_t    sizeinmb;
            uint16_t    ataversion;
            uint8_t     version, model[41];
            int         i;

            switch (type) {
            case DSK_TYPE_ATA:
                sizeinmb = (bios_dsk->devices[device].sectors >> 11);
            case DSK_TYPE_ATAPI:
                // Read ATA/ATAPI version
                ataversion = ((uint16_t)(*(buffer+161))<<8) | *(buffer+160);
                for (version = 15; version > 0; version--) {
                    if ((ataversion & (1 << version)) !=0 )
                        break;
                }

                // Read model name
                for (i = 0; i < 20; i++ ) {
                    *(model+(i*2))   = *(buffer+(i*2)+54+1);
                    *(model+(i*2)+1) = *(buffer+(i*2)+54);
                }

                // Reformat
                *(model+40) = 0x00;
                for ( i = 39; i > 0; i-- ){
                    if (*(model+i) == 0x20)
                        *(model+i) = 0x00;
                    else
                        break;
                }
                break;
            }

#ifdef VBOXz
            // we don't want any noisy output for now
#else /* !VBOX */
            switch (type) {
            int c;
            case DSK_TYPE_ATA:
                printf("ata%d %s: ", channel, slave ? " slave" : "master");
                i=0;
                while(c=*(model+i++))
                    printf("%c", c);
                printf(" ATA-%d Hard-Disk (%lu MBytes)\n", version, sizeinmb);
                break;
            case DSK_TYPE_ATAPI:
                printf("ata%d %s: ", channel, slave ? " slave" : "master");
                i=0;
                while(c=*(model+i++))
                    printf("%c", c);
                if (bios_dsk->devices[device].device == DSK_DEVICE_CDROM)
                    printf(" ATAPI-%d CD-ROM/DVD-ROM\n", version);
                else
                    printf(" ATAPI-%d Device\n", version);
                break;
            case DSK_TYPE_UNKNOWN:
                printf("ata%d %s: Unknown device\n", channel , slave ? " slave" : "master");
                break;
            }
#endif /* !VBOX */
        }
    }

    // Store the devices counts
    bios_dsk->hdcount = hdcount;
    bios_dsk->cdcount = cdcount;
    write_byte(0x40,0x75, hdcount);

#ifdef VBOX
      // we don't want any noisy output for now
#else /* !VBOX */
  printf("\n");
#endif /* !VBOX */

    // FIXME : should use bios=cmos|auto|disable bits
    // FIXME : should know about translation bits
    // FIXME : move hard_drive_post here

}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a data-out command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : BUSY bit set
      // 2 : read error
      // 3 : expected DRQ=1
      // 4 : no sectors left to read/verify
      // 5 : more sectors to read/verify
      // 6 : no sectors left to write
      // 7 : more sectors to write
uint16_t ata_cmd_data_out(bio_dsk_t __far *bios_dsk, uint16_t command, uint16_t count)
{
    uint64_t        lba;
    char __far      *buffer;
    uint16_t        iobase1, iobase2, blksize;
    uint16_t        cylinder;
    uint16_t        head;
    uint16_t        sector;
    uint16_t        device;
    uint8_t         channel, slave;
    uint8_t         status, mode;

    device  = bios_dsk->drqp.dev_id;
    channel = device / 2;
    slave   = device % 2;

    iobase1 = bios_dsk->channels[channel].iobase1;
    iobase2 = bios_dsk->channels[channel].iobase2;
    mode    = bios_dsk->devices[device].mode;
    blksize = 0x200; // was = bios_dsk->devices[device].blksize;
#if VBOX_BIOS_CPU >= 80386
    if (mode == ATA_MODE_PIO32)
        blksize >>= 2;
    else
#endif
        blksize >>= 1;

    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
    {
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 1;
    }

    lba      = bios_dsk->drqp.lba;
    buffer   = bios_dsk->drqp.buffer;
    sector   = bios_dsk->drqp.sector;
    cylinder = bios_dsk->drqp.cylinder;
    head     = bios_dsk->drqp.head;

    // sector will be 0 only on lba access. Convert to lba-chs
    if (sector == 0) {
        if (lba + count >= 268435456)
        {
            sector = (lba >> 24) & 0x00ff;
            cylinder = (lba >> 32) & 0xffff;
            outb(iobase1 + ATA_CB_SC, (count & 0xff00) >> 8);
            outb(iobase1 + ATA_CB_SN, sector);
            outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
            outb(iobase1 + ATA_CB_CH, cylinder >> 8);
            /* Leave the bottom 24 bits as is, they are treated correctly by the
            * LBA28 code path. */
            lba &= 0xffffff;
        }
        sector = (uint16_t) (lba & 0x000000ffL);
        lba >>= 8;
        cylinder = (uint16_t) (lba & 0x0000ffffL);
        lba >>= 16;
        head = ((uint16_t) (lba & 0x0000000fL)) | 0x40;
    }

    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    outb(iobase1 + ATA_CB_FR, 0x00);
    outb(iobase1 + ATA_CB_SC, count);
    outb(iobase1 + ATA_CB_SN, sector);
    outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
    outb(iobase1 + ATA_CB_CH, cylinder >> 8);
    outb(iobase1 + ATA_CB_DH, (slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0) | (uint8_t) head );
    outb(iobase1 + ATA_CB_CMD, command);

    while (1) {
        status = inb(iobase1 + ATA_CB_STAT);
        if ( !(status & ATA_CB_STAT_BSY) )
            break;
    }

    if (status & ATA_CB_STAT_ERR) {
        BX_DEBUG_ATA("%s: write error\n", __func__);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("%s: DRQ not set (status %02x)\n", __func__, (unsigned) status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 3;
    }

    // FIXME : move seg/off translation here

    int_enable(); // enable higher priority interrupts

    while (1) {

        // adjust if there will be an overrun. 2K max sector size
        if (FP_OFF(buffer) >= 0xF800)
            buffer = MK_FP(FP_SEG(buffer) + 0x80, FP_OFF(buffer) - 0x800);

#if VBOX_BIOS_CPU >= 80386
        if (mode == ATA_MODE_PIO32)
            buffer = rep_outsd(buffer, blksize, iobase1);
        else
#endif
            buffer = rep_outsw(buffer, blksize, iobase1);

        bios_dsk->drqp.trsfsectors++;
        count--;
        while (1) {
            status = inb(iobase1 + ATA_CB_STAT);
            if ( !(status & ATA_CB_STAT_BSY) )
                break;
        }
        if (count == 0) {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
              != ATA_CB_STAT_RDY ) {
                BX_DEBUG_ATA("%s: no sectors left (status %02x)\n", __func__, (unsigned) status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 6;
            }
            break;
        }
        else {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
              != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                BX_DEBUG_ATA("%s: more sectors left (status %02x)\n", __func__, (unsigned) status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 7;
            }
            continue;
        }
    }
    // Enable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
    return 0;
}


/**
 * Read sectors from an attached ATA device.
 *
 * @returns status code.
 * @param   bios_dsk    Pointer to disk request packet (in the
 *                      EBDA).
 */
int ata_read_sectors(bio_dsk_t __far *bios_dsk)
{
    uint16_t    n_sect;
    int         status;
    uint8_t     device_id;

    device_id = bios_dsk->drqp.dev_id;
    n_sect    = bios_dsk->drqp.nsect;

    if (bios_dsk->drqp.sector) {
        /* CHS addressing. */
        bios_dsk->devices[device_id].blksize = n_sect * 0x200;
        BX_DEBUG_ATA("%s: reading %u sectors (CHS)\n", __func__, n_sect);
        status = ata_cmd_data_in(bios_dsk, ATA_CMD_READ_MULTIPLE, n_sect);
        bios_dsk->devices[device_id].blksize = 0x200;
    } else {
        /* LBA addressing. */
        if (bios_dsk->drqp.lba + n_sect >= 268435456) {
            BX_DEBUG_ATA("%s: reading %u sector (LBA,EXT)\n", __func__, n_sect);
            status = ata_cmd_data_in(bios_dsk, ATA_CMD_READ_SECTORS_EXT, n_sect);
        } else {
            bios_dsk->devices[device_id].blksize = n_sect * 0x200;
            BX_DEBUG_ATA("%s: reading %u sector (LBA,MULT)\n", __func__, n_sect);
            status = ata_cmd_data_in(bios_dsk, ATA_CMD_READ_MULTIPLE, n_sect);
            bios_dsk->devices[device_id].blksize = 0x200;
        }
    }
    return status;
}

/**
 * Write sectors to an attached ATA device.
 *
 * @returns status code.
 * @param   bios_dsk    Pointer to disk request packet (in the
 *                      EBDA).
 */
int ata_write_sectors(bio_dsk_t __far *bios_dsk)
{
    uint16_t    n_sect;

    n_sect = bios_dsk->drqp.nsect;

    if (bios_dsk->drqp.sector) {
        /* CHS addressing. */
        return ata_cmd_data_out(bios_dsk, ATA_CMD_WRITE_SECTORS, n_sect);
    } else {
        /* LBA addressing. */
        if (bios_dsk->drqp.lba + n_sect >= 268435456)
            return ata_cmd_data_out(bios_dsk, ATA_CMD_WRITE_SECTORS_EXT, n_sect);
        else
            return ata_cmd_data_out(bios_dsk, ATA_CMD_WRITE_SECTORS, n_sect);
    }
}


// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a packet command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : error in parameters
      // 2 : BUSY bit set
      // 3 : error
      // 4 : not ready
uint16_t ata_cmd_packet(uint16_t device, uint8_t cmdlen, char __far *cmdbuf,
                        uint32_t length, uint8_t inout, char __far *buffer)
{
    uint16_t        iobase1, iobase2;
    uint16_t        lcount, count;
    uint8_t         channel, slave;
    uint8_t         status, mode, lmode;
    uint32_t        transfer;
    bio_dsk_t __far *bios_dsk;

    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;

    channel = device / 2;
    slave = device % 2;

    // Data out is not supported yet
    if (inout == ATA_DATA_OUT) {
        BX_INFO("%s: DATA_OUT not supported yet\n", __func__);
        return 1;
    }

    iobase1  = bios_dsk->channels[channel].iobase1;
    iobase2  = bios_dsk->channels[channel].iobase2;
    mode     = bios_dsk->devices[device].mode;
    transfer = 0L;

    if (cmdlen < 12)
        cmdlen = 12;
    if (cmdlen > 12)
        cmdlen = 16;
    cmdlen >>= 1;

    // Reset count of transferred data
    /// @todo clear in calling code?
    bios_dsk->drqp.trsfsectors = 0;
    bios_dsk->drqp.trsfbytes   = 0;

    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
        return 2;

    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    // outb(iobase1 + ATA_CB_FR, 0x00);
    // outb(iobase1 + ATA_CB_SC, 0x00);
    // outb(iobase1 + ATA_CB_SN, 0x00);
    outb(iobase1 + ATA_CB_CL, 0xfff0 & 0x00ff);
    outb(iobase1 + ATA_CB_CH, 0xfff0 >> 8);
    outb(iobase1 + ATA_CB_DH, slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
    outb(iobase1 + ATA_CB_CMD, ATA_CMD_PACKET);

    // Device should ok to receive command
    while (1) {
        status = inb(iobase1 + ATA_CB_STAT);
        if ( !(status & ATA_CB_STAT_BSY) ) break;
    }

    if (status & ATA_CB_STAT_CHK) {
        BX_DEBUG_ATA("%s: error, status is %02x\n", __func__, status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 3;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("%s: DRQ not set (status %02x)\n", __func__, (unsigned) status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 4;
    }

    int_enable(); // enable higher priority interrupts

    // Normalize address
    BX_DEBUG_ATA("acp1 buffer ptr: %04x:%04x wlen %04x\n", FP_SEG(cmdbuf), FP_OFF(cmdbuf), cmdlen);
    cmdbuf = MK_FP(FP_SEG(cmdbuf) + FP_OFF(cmdbuf) / 16 , FP_OFF(cmdbuf) % 16);
    //  cmdseg += (cmdoff / 16);
    //  cmdoff %= 16;

    // Send command to device
    rep_outsw(cmdbuf, cmdlen, iobase1);

    if (inout == ATA_DATA_NO) {
        status = inb(iobase1 + ATA_CB_STAT);
    }
    else {
        while (1) {

            while (1) {
                status = inb(iobase1 + ATA_CB_STAT);
                if ( !(status & ATA_CB_STAT_BSY) )
                    break;
            }

            // Check if command completed
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_DRQ) ) ==0 )
                break;

            if (status & ATA_CB_STAT_CHK) {
                BX_DEBUG_ATA("%s: error (status %02x)\n", __func__, status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 3;
            }

            // Device must be ready to send data
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_CHK) )
              != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                BX_DEBUG_ATA("%s: not ready (status %02x)\n", __func__, status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 4;
            }

            // Normalize address
            BX_DEBUG_ATA("acp2 buffer ptr: %04x:%04x\n", FP_SEG(buffer), FP_OFF(buffer));
            buffer = MK_FP(FP_SEG(buffer) + FP_OFF(buffer) / 16 , FP_OFF(buffer) % 16);
            //      bufseg += (bufoff / 16);
            //      bufoff %= 16;

            // Get the byte count
            lcount =  ((uint16_t)(inb(iobase1 + ATA_CB_CH))<<8)+inb(iobase1 + ATA_CB_CL);

            // Save byte count
            count = lcount;

            BX_DEBUG_ATA("Trying to read %04x bytes ",lcount);
            BX_DEBUG_ATA("to 0x%04x:0x%04x\n",FP_SEG(buffer),FP_OFF(buffer));

            // If counts not dividable by 4, use 16bits mode
            lmode = mode;
            if (lcount  & 0x03)
                lmode = ATA_MODE_PIO16;

            // adds an extra byte if count are odd. before is always even
            if (lcount & 0x01) {
                lcount += 1;
            }

#if VBOX_BIOS_CPU >= 80386
            if (lmode == ATA_MODE_PIO32) {
                lcount  >>= 2;
            } else
#endif
            {
                lcount  >>= 1;
            }

#if VBOX_BIOS_CPU >= 80386
            if (lmode == ATA_MODE_PIO32) {
                rep_insd(buffer, lcount, iobase1);
            } else
#endif
            {
                rep_insw(buffer, lcount, iobase1);
            }


            // Compute new buffer address
            buffer += count;

            // Save transferred bytes count
            transfer += count;
            bios_dsk->drqp.trsfbytes = transfer;
        }
    }

    // Final check, device must be ready
    if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF | ATA_CB_STAT_DRQ | ATA_CB_STAT_CHK) )
      != ATA_CB_STAT_RDY ) {
        BX_DEBUG_ATA("%s: not ready (status %02x)\n", __func__, (unsigned) status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 4;
    }

    // Enable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
    return 0;
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : reset device; intended for ATAPI devices
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : error
uint16_t ata_soft_reset(uint16_t device)
{
    uint16_t        iobase1, iobase2;
    uint8_t         channel, slave;
    uint8_t         status;
    bio_dsk_t __far *bios_dsk;

    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;

    channel = device / 2;
    slave   = device % 2;

    iobase1  = bios_dsk->channels[channel].iobase1;
    iobase2  = bios_dsk->channels[channel].iobase2;

    /* Send a reset command to the device. */
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    outb(iobase1 + ATA_CB_DH, slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
    outb(iobase1 + ATA_CB_CMD, ATA_CMD_DEVICE_RESET);

    /* Wait for the device to clear BSY. */
    while (1) {
        status = inb(iobase1 + ATA_CB_STAT);
        if ( !(status & ATA_CB_STAT_BSY) ) break;
    }

    /* Final check, device must be ready */
    if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF | ATA_CB_STAT_DRQ | ATA_CB_STAT_CHK) )
      != ATA_CB_STAT_RDY ) {
        BX_DEBUG_ATA("%s: not ready (status %02x)\n", __func__, (unsigned) status);
        /* Enable interrupts */
        outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15);
        return 1;
    }

    /* Enable interrupts */
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
    return 0;
}


// ---------------------------------------------------------------------------
// End of ATA/ATAPI Driver
// ---------------------------------------------------------------------------
