/* $Id: disk.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PC BIOS - ???
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
#include "biosint.h"
#include "inlines.h"
#include "ebda.h"
#include "ata.h"


#if DEBUG_INT13_HD
#  define BX_DEBUG_INT13_HD(...) BX_DEBUG(__VA_ARGS__)
#else
#  define BX_DEBUG_INT13_HD(...)
#endif

/* Generic disk read/write routine signature. */
typedef int __fastcall (* dsk_rw_func)(bio_dsk_t __far *bios_dsk);

/* Controller specific disk access routines. Declared as a union to reduce
 * the need for conditionals when choosing between read/write functions.
 * Note that we get away with using near pointers, which is nice.
 */
typedef union {
    struct {
        dsk_rw_func     read;
        dsk_rw_func     write;
    } s;
    dsk_rw_func a[2];
} dsk_acc_t;

/* Pointers to HW specific disk access routines. */
dsk_acc_t   dskacc[DSKTYP_CNT] = {
    [DSK_TYPE_ATA]  = { ata_read_sectors,  ata_write_sectors },
#ifdef VBOX_WITH_AHCI
    [DSK_TYPE_AHCI] = { ahci_read_sectors, ahci_write_sectors },
#endif
#ifdef VBOX_WITH_SCSI
    [DSK_TYPE_SCSI] = { scsi_read_sectors, scsi_write_sectors },
#endif
};


/// @todo put in a header
#define AX      r.gr.u.r16.ax
#define BX      r.gr.u.r16.bx
#define CX      r.gr.u.r16.cx
#define DX      r.gr.u.r16.dx
#define SI      r.gr.u.r16.si
#define DI      r.gr.u.r16.di
#define BP      r.gr.u.r16.bp
#define ELDX    r.gr.u.r16.sp
#define DS      r.ds
#define ES      r.es
#define FLAGS   r.ra.flags.u.r16.flags


/*
 * Build translated CHS geometry given a disk size in sectors. Based on
 * Phoenix EDD 3.0. This is used as a fallback to generate sane logical
 * geometry in case none was provided in CMOS.
 */
void set_geom_lba(chs_t __far *lgeo, uint64_t nsectors64)
{
    uint32_t    limit = 8257536;    /* 1024 * 128 * 63 */
    uint32_t    nsectors;
    unsigned    heads = 255;
    int         i;

    nsectors = (nsectors64 >> 32) ? 0xFFFFFFFFL : (uint32_t)nsectors64;
    /* Start with ~4GB limit, go down to 504MB. */
    for (i = 0; i < 4; ++i) {
        if (nsectors <= limit)
            heads = (heads + 1) / 2;
        limit /= 2;
    }

    lgeo->cylinders = nsectors / (heads * 63UL);
    if (lgeo->cylinders > 1024)
        lgeo->cylinders = 1024;
    lgeo->heads     = heads;
    lgeo->spt       = 63;   /* Always 63 sectors per track, the maximum. */
}

int edd_fill_dpt(dpt_t __far *dpt, bio_dsk_t __far *bios_dsk, uint8_t device)
{
    uint16_t    ebda_seg = read_word(0x0040,0x000E);

    /* Check if buffer is large enough. */
    if (dpt->size < 0x1a)
        return -1;

    /* Fill in EDD 1.x table. */
    if (dpt->size >= 0x1a) {
        uint64_t    lba;

        dpt->size      = 0x1a;
        dpt->blksize   = bios_dsk->devices[device].blksize;

        if (bios_dsk->devices[device].device == DSK_DEVICE_CDROM) {
            dpt->infos         = 0x74;  /* Removable, media change, lockable, max values */
            dpt->cylinders     = 0xffffffff;
            dpt->heads         = 0xffffffff;
            dpt->spt           = 0xffffffff;
            dpt->sector_count1 = 0xffffffff;
            dpt->sector_count2 = 0xffffffff;
        } else {
            dpt->infos     = 0x02;  // geometry is valid
            dpt->cylinders = bios_dsk->devices[device].pchs.cylinders;
            dpt->heads     = bios_dsk->devices[device].pchs.heads;
            dpt->spt       = bios_dsk->devices[device].pchs.spt;
            lba = bios_dsk->devices[device].sectors;
            dpt->sector_count1 = lba;
            dpt->sector_count2 = lba >> 32;
        }
    }

    /* Fill in EDD 2.x table. */
    if (dpt->size >= 0x1e) {
        uint8_t     channel, irq, mode, checksum, i, xlation;
        uint16_t    iobase1, iobase2, options;

        dpt->size = 0x1e;
        dpt->dpte_segment = ebda_seg;
        dpt->dpte_offset  = (uint16_t)&EbdaData->bdisk.dpte;

        // Fill in dpte
        channel = device / 2;
        iobase1 = bios_dsk->channels[channel].iobase1;
        iobase2 = bios_dsk->channels[channel].iobase2;
        irq     = bios_dsk->channels[channel].irq;
        mode    = bios_dsk->devices[device].mode;
        xlation = bios_dsk->devices[device].translation;

        options  = (xlation == GEO_TRANSLATION_NONE ? 0 : 1 << 3);  /* CHS translation */
        options |= (1 << 4);    /* LBA translation */
        if (bios_dsk->devices[device].device == DSK_DEVICE_CDROM) {
            options |= (1 << 5);    /* Removable device */
            options |= (1 << 6);    /* ATAPI device */
        }
#if VBOX_BIOS_CPU >= 80386
        options |= (mode == ATA_MODE_PIO32 ? 1 : 0 << 7);
#endif
        options |= (xlation == GEO_TRANSLATION_LBA ? 1 : 0 << 9);
        options |= (xlation == GEO_TRANSLATION_RECHS ? 3 : 0 << 9);

        bios_dsk->dpte.iobase1  = iobase1;
        bios_dsk->dpte.iobase2  = iobase2;
        bios_dsk->dpte.prefix   = (0xe | (device % 2)) << 4;
        bios_dsk->dpte.unused   = 0xcb;
        bios_dsk->dpte.irq      = irq;
        bios_dsk->dpte.blkcount = 1;
        bios_dsk->dpte.dma      = 0;
        bios_dsk->dpte.pio      = 0;
        bios_dsk->dpte.options  = options;
        bios_dsk->dpte.reserved = 0;
        bios_dsk->dpte.revision = 0x11;

        checksum = 0;
        for (i = 0; i < 15; ++i)
            checksum += read_byte(ebda_seg, (uint16_t)&EbdaData->bdisk.dpte + i);
        checksum = -checksum;
        bios_dsk->dpte.checksum = checksum;
    }

    /* Fill in EDD 3.x table. */
    if (dpt->size >= 0x42) {
        uint8_t     channel, iface, checksum, i;
        uint16_t    iobase1;

        channel = device / 2;
        iface   = bios_dsk->channels[channel].iface;
        iobase1 = bios_dsk->channels[channel].iobase1;

        dpt->size       = 0x42;
        dpt->key        = 0xbedd;
        dpt->dpi_length = 0x24;
        dpt->reserved1  = 0;
        dpt->reserved2  = 0;

        if (iface == ATA_IFACE_ISA) {
            dpt->host_bus[0] = 'I';
            dpt->host_bus[1] = 'S';
            dpt->host_bus[2] = 'A';
            dpt->host_bus[3] = ' ';
        }
        else {
            // FIXME PCI
        }
        dpt->iface_type[0] = 'A';
        dpt->iface_type[1] = 'T';
        dpt->iface_type[2] = 'A';
        dpt->iface_type[3] = ' ';
        dpt->iface_type[4] = ' ';
        dpt->iface_type[5] = ' ';
        dpt->iface_type[6] = ' ';
        dpt->iface_type[7] = ' ';

        if (iface == ATA_IFACE_ISA) {
            ((uint16_t __far *)dpt->iface_path)[0] = iobase1;
            ((uint16_t __far *)dpt->iface_path)[1] = 0;
            ((uint32_t __far *)dpt->iface_path)[1] = 0;
        }
        else {
            // FIXME PCI
        }
        ((uint16_t __far *)dpt->device_path)[0] = device & 1; // device % 2; @todo: correct?
        ((uint16_t __far *)dpt->device_path)[1] = 0;
        ((uint32_t __far *)dpt->device_path)[1] = 0;

        checksum = 0;
        for (i = 30; i < 64; i++)
            checksum += ((uint8_t __far *)dpt)[i];
        checksum = -checksum;
        dpt->checksum = checksum;
    }
    return 0;
}

void BIOSCALL int13_harddisk(disk_regs_t r)
{
    uint32_t            lba;
    uint16_t            cylinder, head, sector;
    uint16_t            nlc, nlh, nlspt;
    uint16_t            count;
    uint8_t             device, status;
    bio_dsk_t __far     *bios_dsk;

    BX_DEBUG_INT13_HD("%s: AX=%04x BX=%04x CX=%04x DX=%04x ES=%04x\n", __func__, AX, BX, CX, DX, ES);

    SET_IF();   /* INT 13h always returns with interrupts enabled. */

    bios_dsk = read_word(0x0040,0x000E) :> &EbdaData->bdisk;
    write_byte(0x0040, 0x008e, 0);  // clear completion flag

    // basic check : device has to be defined
    if ( (GET_ELDL() < 0x80) || (GET_ELDL() >= 0x80 + BX_MAX_STORAGE_DEVICES) ) {
        BX_DEBUG("%s: function %02x, ELDL out of range %02x\n", __func__, GET_AH(), GET_ELDL());
        goto int13_fail;
    }

    // Get the ata channel
    device = bios_dsk->hdidmap[GET_ELDL()-0x80];

    // basic check : device has to be valid
    if (device >= BX_MAX_STORAGE_DEVICES) {
        BX_DEBUG("%s: function %02x, unmapped device for ELDL=%02x\n", __func__, GET_AH(), GET_ELDL());
        goto int13_fail;
    }

    switch (GET_AH()) {

    case 0x00: /* disk controller reset */
        /* For ATA drives only. NB: This resets both drives on a channel. */
        if (VBOX_IS_ATA_DEVICE(device))
            ata_reset(device);
        goto int13_success;
        break;

    case 0x01: /* read disk status */
        status = read_byte(0x0040, 0x0074);
        SET_AH(status);
        SET_DISK_RET_STATUS(0);
        /* set CF if error status read */
        if (status) goto int13_fail_nostatus;
        else        goto int13_success_noah;
        break;

    case 0x02: // read disk sectors
    case 0x03: // write disk sectors
    case 0x04: // verify disk sectors

        count       = GET_AL();
        cylinder    = GET_CH();
        cylinder   |= ( ((uint16_t) GET_CL()) << 2) & 0x300;
        sector      = (GET_CL() & 0x3f);
        head        = GET_DH();

        /* Segment and offset are in ES:BX. */
        if ( (count > 128) || (count == 0) ) {
            BX_INFO("%s: function %02x, count out of range!\n", __func__, GET_AH());
            goto int13_fail;
        }

        /* Get the logical CHS geometry. */
        nlc   = bios_dsk->devices[device].lchs.cylinders;
        nlh   = bios_dsk->devices[device].lchs.heads;
        nlspt = bios_dsk->devices[device].lchs.spt;

        /* Sanity check the geometry. */
        if( (cylinder >= nlc) || (head >= nlh) || (sector > nlspt )) {
            BX_INFO("%s: function %02x, disk %02x, parameters out of range %04x/%04x/%04x!\n", __func__, GET_AH(), GET_DL(), cylinder, head, sector);
            goto int13_fail;
        }

        // FIXME verify
        if ( GET_AH() == 0x04 )
            goto int13_success;

        /* If required, translate LCHS to LBA and execute command. */
        /// @todo The IS_SCSI_DEVICE check should be redundant...
        if (( (bios_dsk->devices[device].pchs.heads != nlh) || (bios_dsk->devices[device].pchs.spt != nlspt)) || VBOX_IS_SCSI_DEVICE(device)) {
            lba = ((((uint32_t)cylinder * (uint32_t)nlh) + (uint32_t)head) * (uint32_t)nlspt) + (uint32_t)sector - 1;
            sector = 0; // this forces the command to be lba
            BX_DEBUG_INT13_HD("%s: %d sectors from lba %lu @ %04x:%04x\n", __func__,
                              count, lba, ES, BX);
        } else {
            BX_DEBUG_INT13_HD("%s: %d sectors from C/H/S %u/%u/%u @ %04x:%04x\n", __func__,
                              count, cylinder, head, sector, ES, BX);
        }


        /* Clear the count of transferred sectors/bytes. */
        bios_dsk->drqp.trsfsectors = 0;
        bios_dsk->drqp.trsfbytes   = 0;

        /* Pass request information to low level disk code. */
        bios_dsk->drqp.lba      = lba;
        bios_dsk->drqp.buffer   = MK_FP(ES, BX);
        bios_dsk->drqp.nsect    = count;
        bios_dsk->drqp.sect_sz  = 512;  /// @todo device specific?
        bios_dsk->drqp.cylinder = cylinder;
        bios_dsk->drqp.head     = head;
        bios_dsk->drqp.sector   = sector;
        bios_dsk->drqp.dev_id   = device;

        status = dskacc[bios_dsk->devices[device].type].a[GET_AH() - 0x02](bios_dsk);

        // Set nb of sector transferred
        SET_AL(bios_dsk->drqp.trsfsectors);

        if (status != 0) {
            BX_INFO("%s: function %02x, error %02x !\n", __func__, GET_AH(), status);
            SET_AH(0x0c);
            goto int13_fail_noah;
        }

        goto int13_success;
        break;

    case 0x05: /* format disk track */
        BX_INFO("format disk track called\n");
        goto int13_success;
        break;

    case 0x08: /* read disk drive parameters */

        /* Get the logical geometry from internal table. */
        nlc   = bios_dsk->devices[device].lchs.cylinders;
        nlh   = bios_dsk->devices[device].lchs.heads;
        nlspt = bios_dsk->devices[device].lchs.spt;

        count = bios_dsk->hdcount;
        /* Maximum cylinder number is just one less than the number of cylinders. */
        /* To make Windows 3.1x WDCTRL.386 happy, we'd have to subtract 2, not 1,
         * to account for a diagnostic cylinder.
         */
        nlc = nlc - 1; /* 0 based , last sector not used */
        SET_AL(0);
        SET_CH(nlc & 0xff);
        SET_CL(((nlc >> 2) & 0xc0) | (nlspt & 0x3f));
        SET_DH(nlh - 1);
        SET_DL(count); /* FIXME returns 0, 1, or n hard drives */

        // FIXME should set ES & DI
        /// @todo Actually, the above comment is nonsense.

        goto int13_success;
        break;

    case 0x10: /* check drive ready */
        // should look at 40:8E also???

#ifdef VBOX_WITH_SCSI
        /* SCSI drives are always "ready". */
        if (!VBOX_IS_SCSI_DEVICE(device)) {
#endif
        // Read the status from controller
        status = inb(bios_dsk->channels[device/2].iobase1 + ATA_CB_STAT);
        if ( (status & ( ATA_CB_STAT_BSY | ATA_CB_STAT_RDY )) == ATA_CB_STAT_RDY ) {
            goto int13_success;
        } else {
            SET_AH(0xAA);
            goto int13_fail_noah;
        }
#ifdef VBOX_WITH_SCSI
        } else  /* It's not an ATA drive. */
            goto int13_success;
#endif
        break;

    case 0x15: /* read disk drive size */

        /* Get the physical geometry from internal table. */
        cylinder = bios_dsk->devices[device].pchs.cylinders;
        head     = bios_dsk->devices[device].pchs.heads;
        sector   = bios_dsk->devices[device].pchs.spt;

        /* Calculate sector count seen by old style INT 13h. */
        lba = (uint32_t)cylinder * head * sector;
        CX = lba >> 16;
        DX = lba & 0xffff;

        SET_AH(3);  // hard disk accessible
        goto int13_success_noah;
        break;

    case 0x09: /* initialize drive parameters */
    case 0x0c: /* seek to specified cylinder */
    case 0x0d: /* alternate disk reset */
    case 0x11: /* recalibrate */
    case 0x14: /* controller internal diagnostic */
        BX_INFO("%s: function %02xh unimplemented, returns success\n", __func__, GET_AH());
        goto int13_success;
        break;

    case 0x0a: /* read disk sectors with ECC */
    case 0x0b: /* write disk sectors with ECC */
    case 0x18: // set media type for format
    default:
        BX_INFO("%s: function %02xh unsupported, returns fail\n", __func__, GET_AH());
        goto int13_fail;
        break;
    }

int13_fail:
    SET_AH(0x01); // defaults to invalid function in AH or invalid parameter
int13_fail_noah:
    SET_DISK_RET_STATUS(GET_AH());
int13_fail_nostatus:
    SET_CF();     // error occurred
    return;

int13_success:
    SET_AH(0x00); // no error
int13_success_noah:
    SET_DISK_RET_STATUS(0x00);
    CLEAR_CF();   // no error
    return;
}

void BIOSCALL int13_harddisk_ext(disk_regs_t r)
{
    uint64_t            lba;
    uint16_t            segment, offset;
    uint8_t             device, status;
    uint16_t            count;
    uint8_t             type;
    bio_dsk_t __far     *bios_dsk;
    int13ext_t __far    *i13_ext;
#if 0
    uint16_t            ebda_seg = read_word(0x0040,0x000E);
    uint16_t            npc, nph, npspt;
    uint16_t            size;
    dpt_t __far         *dpt;
#endif

    bios_dsk = read_word(0x0040,0x000E) :> &EbdaData->bdisk;

    BX_DEBUG_INT13_HD("%s: AX=%04x BX=%04x CX=%04x DX=%04x ES=%04x DS=%04x SI=%04x\n",
                      __func__, AX, BX, CX, DX, ES, DS, SI);

    write_byte(0x0040, 0x008e, 0);  // clear completion flag

    // basic check : device has to be defined
    if ( (GET_ELDL() < 0x80) || (GET_ELDL() >= 0x80 + BX_MAX_STORAGE_DEVICES) ) {
        BX_DEBUG("%s: function %02x, ELDL out of range %02x\n", __func__, GET_AH(), GET_ELDL());
        goto int13x_fail;
    }

    // Get the ata channel
    device = bios_dsk->hdidmap[GET_ELDL()-0x80];

    // basic check : device has to be valid
    if (device >= BX_MAX_STORAGE_DEVICES) {
        BX_DEBUG("%s: function %02x, unmapped device for ELDL=%02x\n", __func__, GET_AH(), GET_ELDL());
        goto int13x_fail;
    }

    switch (GET_AH()) {
    case 0x41: // IBM/MS installation check
        BX=0xaa55;     // install check
        SET_AH(0x30);  // EDD 3.0
        CX=0x0007;     // ext disk access and edd, removable supported
        goto int13x_success_noah;
        break;

    case 0x42: // IBM/MS extended read
    case 0x43: // IBM/MS extended write
    case 0x44: // IBM/MS verify
    case 0x47: // IBM/MS extended seek

        /* Get a pointer to the extended structure. */
        i13_ext = DS :> (int13ext_t *)SI;

        count   = i13_ext->count;
        segment = i13_ext->segment;
        offset  = i13_ext->offset;

        // Get 64 bits lba and check
        lba = i13_ext->lba2;
        lba <<= 32;
        lba |= i13_ext->lba1;

        BX_DEBUG_INT13_HD("%s: %d sectors from LBA 0x%llx @ %04x:%04x\n", __func__,
                          count, lba, segment, offset);

        type = bios_dsk->devices[device].type;
        if (lba >= bios_dsk->devices[device].sectors) {
              BX_INFO("%s: function %02x. LBA out of range\n", __func__, GET_AH());
              goto int13x_fail;
        }

        /* Don't bother with seek or verify. */
        if (( GET_AH() == 0x44 ) || ( GET_AH() == 0x47 ))
            goto int13x_success;

        /* Clear the count of transferred sectors/bytes. */
        bios_dsk->drqp.trsfsectors = 0;
        bios_dsk->drqp.trsfbytes   = 0;

        /* Pass request information to low level disk code. */
        bios_dsk->drqp.lba     = lba;
        bios_dsk->drqp.buffer  = MK_FP(segment, offset);
        bios_dsk->drqp.nsect   = count;
        bios_dsk->drqp.sect_sz = 512;   /// @todo device specific?
        bios_dsk->drqp.sector  = 0;     /* Indicate LBA. */
        bios_dsk->drqp.dev_id  = device;

        /* Execute the read or write command. */
        status = dskacc[type].a[GET_AH() - 0x42](bios_dsk);
        count  = bios_dsk->drqp.trsfsectors;
        i13_ext->count = count;

        if (status != 0) {
            BX_INFO("%s: function %02x, error %02x !\n", __func__, GET_AH(), status);
            SET_AH(0x0c);
            goto int13x_fail_noah;
        }

        goto int13x_success;
        break;

    case 0x45: // IBM/MS lock/unlock drive
    case 0x49: // IBM/MS extended media change
        goto int13x_success;   // Always success for HD
        break;

    case 0x46: // IBM/MS eject media
        SET_AH(0xb2);          // Volume Not Removable
        goto int13x_fail_noah; // Always fail for HD
        break;

    case 0x48: // IBM/MS get drive parameters
        if (edd_fill_dpt(DS :> (dpt_t *)SI, bios_dsk, device))
            goto int13x_fail;
        else
            goto int13x_success;
        break;

    case 0x4e: // // IBM/MS set hardware configuration
        // DMA, prefetch, PIO maximum not supported
        switch (GET_AL()) {
        case 0x01:
        case 0x03:
        case 0x04:
        case 0x06:
            goto int13x_success;
            break;
        default :
            goto int13x_fail;
        }
        break;

    case 0x50: // IBM/MS send packet command
    default:
        BX_INFO("%s: function %02xh unsupported, returns fail\n", __func__, GET_AH());
        goto int13x_fail;
        break;
    }

int13x_fail:
    SET_AH(0x01); // defaults to invalid function in AH or invalid parameter
int13x_fail_noah:
    SET_DISK_RET_STATUS(GET_AH());
    SET_CF();     // error occurred
    return;

int13x_success:
    SET_AH(0x00); // no error
int13x_success_noah:
    SET_DISK_RET_STATUS(0x00);
    CLEAR_CF();   // no error
    return;
}

/* Avoid saving general registers already saved by caller (PUSHA). */
#pragma aux int13_harddisk modify [di si cx dx bx];
#pragma aux int13_harddisk_ext modify [di si cx dx bx];

