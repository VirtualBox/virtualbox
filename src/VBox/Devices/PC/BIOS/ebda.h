/* $Id: ebda.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PC BIOS - EBDA (Extended BIOS Data Area) Definition
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

#ifndef VBOX_INCLUDED_SRC_PC_BIOS_ebda_h
#define VBOX_INCLUDED_SRC_PC_BIOS_ebda_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <stdint.h>

/* Must be defined here (EBDA structures depend on these). */
#define BX_MAX_ATA_INTERFACES   4
#define BX_MAX_ATA_DEVICES      (BX_MAX_ATA_INTERFACES*2)

#define BX_USE_ATADRV           1
#define BX_ELTORITO_BOOT        1

#define VBOX_IS_ATA_DEVICE(device_id) (device_id < BX_MAX_ATA_DEVICES)

#ifdef VBOX_WITH_SCSI
    /* Enough for now */
    #define BX_MAX_SCSI_DEVICES 4
    /* A SCSI device starts always at BX_MAX_ATA_DEVICES. */
    #define VBOX_IS_SCSI_DEVICE(device_id) (device_id >= BX_MAX_ATA_DEVICES)
    #define VBOX_GET_SCSI_DEVICE(device_id) (device_id - BX_MAX_ATA_DEVICES)
#else
    #define BX_MAX_SCSI_DEVICES 0
    #define VBOX_IS_SCSI_DEVICE(device_id) (1 == 0)
#endif

#ifdef VBOX_WITH_AHCI
    /* Four should be enough for now */
    #define BX_MAX_AHCI_DEVICES 4

    /* An AHCI device starts always at BX_MAX_ATA_DEVICES + BX_MAX_SCSI_DEVICES. */
    #define VBOX_IS_AHCI_DEVICE(device_id) (device_id >= (BX_MAX_ATA_DEVICES + BX_MAX_SCSI_DEVICES))
    #define VBOX_GET_AHCI_DEVICE(device_id) (device_id - (BX_MAX_ATA_DEVICES + BX_MAX_SCSI_DEVICES))
#else
    #define BX_MAX_AHCI_DEVICES 0
#endif

#define BX_MAX_STORAGE_DEVICES (BX_MAX_ATA_DEVICES + BX_MAX_SCSI_DEVICES + BX_MAX_AHCI_DEVICES)

/* Generic storage device types. These depend on the controller type and
 * determine which device access routines should be called.
 */
enum dsk_type_enm {
    DSK_TYPE_NONE,          /* Unknown device. */
    DSK_TYPE_UNKNOWN,       /* Unknown ATA device. */
    DSK_TYPE_ATA,           /* ATA disk. */
    DSK_TYPE_ATAPI,         /* ATAPI device. */
    DSK_TYPE_SCSI,          /* SCSI disk. */
    DSK_TYPE_AHCI,          /* SATA disk via AHCI. */
    DSKTYP_CNT              /* Number of disk types. */
};

/* Disk device types. */
//@todo: Do we really need these?
#define DSK_DEVICE_NONE     0x00    /* No device attached. */
#define DSK_DEVICE_HD       0xFF    /* Device is a hard disk. */
#define DSK_DEVICE_CDROM    0x05    /* Device is a CD-ROM. */

/* Geometry translation modes. */
enum geo_xlat_enm {
    GEO_TRANSLATION_NONE,   /* No geometry translation. */
    GEO_TRANSLATION_LBA,    /* LBA translation. */
    GEO_TRANSLATION_LARGE,  /* Large CHS translation. */
    GEO_TRANSLATION_RECHS
};

#if 1 //BX_USE_ATADRV

/* Note: The DPTE and FDPT structures are industry standards and
 * may not be modified. The other disk-related structures are
 * internal to the BIOS.
 */

/* Translated DPT (Device Parameter Table). */
typedef struct {
    uint16_t    iobase1;
    uint16_t    iobase2;
    uint8_t     prefix;
    uint8_t     unused;
    uint8_t     irq;
    uint8_t     blkcount;
    uint8_t     dma;
    uint8_t     pio;
    uint16_t    options;
    uint16_t    reserved;
    uint8_t     revision;
    uint8_t     checksum;
} dpte_t;

ct_assert(sizeof(dpte_t) == 16);    /* Ensure correct size. */

#pragma pack(0)

/* FDPT - Fixed Disk Parameter Table. PC/AT compatible; note
 * that this structure is slightly misaligned.
 */
typedef struct {
    uint16_t    lcyl;
    uint8_t     lhead;
    uint8_t     sig;
    uint8_t     spt;
    uint32_t    resvd1;
    uint16_t    cyl;
    uint8_t     head;
    uint16_t    resvd2;
    uint8_t     lspt;
    uint8_t     csum;
} fdpt_t;

#pragma pack()

ct_assert(sizeof(fdpt_t) == 16);    /* Ensure correct size. */


/* C/H/S geometry information. */
typedef struct {
    uint16_t    heads;      /* Number of heads. */
    uint16_t    cylinders;  /* Number of cylinders. */
    uint16_t    spt;        /* Number of sectors per track. */
} chs_t;

/* IDE/ATA specific device information. */
typedef struct {
    uint8_t     iface;      /* ISA or PCI. */
    uint8_t     irq;        /* IRQ (on the PIC). */
    uint16_t    iobase1;    /* I/O base 1. */
    uint16_t    iobase2;    /* I/O base 2. */
} ata_chan_t;

#ifdef VBOX_WITH_SCSI

/* SCSI specific device information. */
typedef struct {
    uint16_t    hba_ofs;        /* Offset (in paragraphs) of HBA driver data block within EBDA. */
    uint8_t     idx_hba;        /* The HBA driver to use. */
    uint8_t     target_id;      /* Target ID. */
} scsi_dev_t;

#endif

#ifdef VBOX_WITH_AHCI

/* AHCI specific device information. */
typedef struct {
    uint8_t     port;           /* SATA port. */
} ahci_dev_t;

#endif

/* Generic disk information. */
typedef struct {
    uint8_t     type;         /* Device type (ATA/ATAPI/SCSI/none/unknown). */
    uint8_t     device;       /* Detected type of attached device (HD/CD/none). */
    uint8_t     removable;    /* Removable device flag. */
    uint8_t     lock;         /* Lock count for removable devices. */
    //@todo: ATA specific - move?
    uint8_t     mode;         /* Transfer mode: PIO 16/32 bits - IRQ - ISADMA - PCIDMA. */
    uint8_t     translation;  /* Type of geometry translation. */
    uint16_t    blksize;      /* Disk block size. */
    chs_t       lchs;         /* Logical CHS geometry. */
    chs_t       pchs;         /* Physical CHS geometry. */
    uint64_t    sectors;      /* Total sector count. */
} disk_dev_t;

/* A structure for passing disk request information around. This structure
 * is designed for saving stack space. As BIOS requests cannot be overlapped,
 * one such structure is sufficient.
 */
typedef struct {
    uint64_t    lba;                /* Starting LBA. */
    void __far  *buffer;            /* Read/write data buffer pointer. */
    uint8_t     dev_id;             /* Device ID; index into devices array. */
    uint16_t    nsect;              /* Number of sectors to be transferred. */
    uint16_t    sect_sz;            /* Size of a sector in bytes. */
    uint16_t    cylinder;           /* Starting cylinder (CHS only). */
    uint16_t    head;               /* Starting head (CHS only). */
    uint16_t    sector;             /* Starting sector (CHS only). */
    uint16_t    trsfsectors;        /* Actual sectors transferred. */
    uint32_t    trsfbytes;          /* Actual bytes transferred. */
} disk_req_t;

extern uint16_t ahci_cmd_packet(uint16_t device_id, uint8_t cmdlen, char __far *cmdbuf,
                                uint32_t length, uint8_t inout, char __far *buffer);
extern uint16_t scsi_cmd_packet(uint16_t device, uint8_t cmdlen, char __far *cmdbuf,
                                uint32_t length, uint8_t inout, char __far *buffer);
extern uint16_t ata_cmd_packet(uint16_t device, uint8_t cmdlen, char __far *cmdbuf,
                               uint32_t length, uint8_t inout, char __far *buffer);

extern uint16_t ata_soft_reset(uint16_t device);

/* All BIOS disk information. Disk-related code in the BIOS should not need
 * anything outside of this structure.
 */
typedef struct {
    disk_req_t  drqp;               /* Disk request packet. */

    /* Bus-independent disk device information. */
    disk_dev_t  devices[BX_MAX_STORAGE_DEVICES];

    uint8_t     hdcount;            /* Total number of BIOS disks. */
    /* Map between (BIOS disk ID - 0x80) and ATA/SCSI/AHCI disks. */
    uint8_t     hdidmap[BX_MAX_STORAGE_DEVICES];

    uint8_t     cdcount;            /* Number of CD-ROMs. */
    /* Map between (BIOS CD-ROM ID - 0xE0) and ATA/SCSI/AHCI devices. */
    uint8_t     cdidmap[BX_MAX_STORAGE_DEVICES];

    /* ATA bus-specific device information. */
    ata_chan_t  channels[BX_MAX_ATA_INTERFACES];

#ifdef VBOX_WITH_SCSI
    /* SCSI bus-specific device information. */
    scsi_dev_t  scsidev[BX_MAX_SCSI_DEVICES];
    uint8_t     scsi_devcount;      /* Number of SCSI devices. */
#endif

#ifdef VBOX_WITH_AHCI
    /* SATA (AHCI) bus-specific device information. */
    ahci_dev_t  ahcidev[BX_MAX_AHCI_DEVICES];
    uint8_t     ahci_devcnt;        /* Number of SATA devices. */
    uint16_t    ahci_seg;           /* Segment of AHCI data block. */
#endif

    dpte_t      dpte;               /* Buffer for building a DPTE. */
} bio_dsk_t;

#if BX_ELTORITO_BOOT
/* El Torito device emulation state. */
typedef struct {
    uint8_t     active;
    uint8_t     media;
    uint8_t     emulated_drive;
    uint8_t     controller_index;
    uint16_t    device_spec;
    uint16_t    buffer_segment;
    uint32_t    ilba;
    uint16_t    load_segment;
    uint16_t    sector_count;
    chs_t       vdevice;        /* Virtual device geometry. */
    uint8_t __far *ptr_unaligned; /* Bounce buffer for sector unaligned reads. */
} cdemu_t;
#endif

// for access to EBDA area
//     The EBDA structure should conform to
//     http://www.frontiernet.net/~fys/rombios.htm document
//     I made the ata and cdemu structs begin at 0x121 in the EBDA seg
/* MS-DOS KEYB.COM may overwrite the word at offset 0x117 in the EBDA
 * which contains the keyboard ID for PS/2 BIOSes.
 */
typedef struct {
    uint8_t     filler1[0x3D];

    fdpt_t      fdpt0;      /* FDPTs for the first two ATA disks. */
    fdpt_t      fdpt1;

    bio_dsk_t   bdisk;      /* Disk driver data (ATA/SCSI/AHCI). */

#if BX_ELTORITO_BOOT
    cdemu_t     cdemu;      /* El Torito floppy/HD emulation data. */
#endif

    unsigned char   uForceBootDrive;
    unsigned char   uForceBootDevice;
} ebda_data_t;

ct_assert(sizeof(ebda_data_t) < 0x380);     /* Must be under 1K in size. */

// the last 16 bytes of the EBDA segment are used for the MPS floating
// pointer structure (though only if an I/O APIC is present)

#define EbdaData ((ebda_data_t *) 0)

// for access to the int13ext structure
typedef struct {
    uint8_t     size;
    uint8_t     reserved;
    uint16_t    count;
    uint16_t    offset;
    uint16_t    segment;
    uint32_t    lba1;
    uint32_t    lba2;
} int13ext_t;

/* Disk Physical Table structure */
typedef struct {
    uint16_t    size;
    uint16_t    infos;
    uint32_t    cylinders;
    uint32_t    heads;
    uint32_t    spt;
    uint32_t    sector_count1;
    uint32_t    sector_count2;
    uint16_t    blksize;
    uint16_t    dpte_offset;
    uint16_t    dpte_segment;
    uint16_t    key;
    uint8_t     dpi_length;
    uint8_t     reserved1;
    uint16_t    reserved2;
    uint8_t     host_bus[4];
    uint8_t     iface_type[8];
    uint8_t     iface_path[8];
    uint8_t     device_path[8];
    uint8_t     reserved3;
    uint8_t     checksum;
} dpt_t;

/* Note: Using fastcall reduces stack usage a little. */
int __fastcall ata_read_sectors(bio_dsk_t __far *bios_dsk);
int __fastcall ata_write_sectors(bio_dsk_t __far *bios_dsk);

int __fastcall scsi_read_sectors(bio_dsk_t __far *bios_dsk);
int __fastcall scsi_write_sectors(bio_dsk_t __far *bios_dsk);

int __fastcall ahci_read_sectors(bio_dsk_t __far *bios_dsk);
int __fastcall ahci_write_sectors(bio_dsk_t __far *bios_dsk);

extern void set_geom_lba(chs_t __far *lgeo, uint64_t nsectors);
extern int edd_fill_dpt(dpt_t __far *dpt, bio_dsk_t __far *bios_dsk, uint8_t device);

// @todo: put this elsewhere (and change/eliminate?)
#define SET_DISK_RET_STATUS(status) write_byte(0x0040, 0x0074, status)

#endif
#endif /* !VBOX_INCLUDED_SRC_PC_BIOS_ebda_h */

