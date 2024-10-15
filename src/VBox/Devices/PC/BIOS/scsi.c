/* $Id: scsi.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SCSI host adapter driver to boot from SCSI disks
 */

/*
 * Copyright (C) 2004-2024 Oracle and/or its affiliates.
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
#include "inlines.h"
#include "pciutil.h"
#include "ebda.h"
#include "scsi.h"


#if DEBUG_SCSI
# define DBG_SCSI(...)  BX_INFO(__VA_ARGS__)
#else
# define DBG_SCSI(...)
#endif

#define VBSCSI_MAX_DEVICES 16 /* Maximum number of devices a SCSI device currently supported. */

#define VBOX_SCSI_NO_HBA    0xffff

typedef uint16_t (* scsi_hba_detect)(void);
typedef int (* scsi_hba_init)(void __far *pvHba, uint8_t u8Bus, uint8_t u8DevFn);
typedef int (* scsi_hba_cmd_data_out)(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                                      uint8_t cbCDB, uint8_t __far *buffer, uint32_t length);
typedef int (* scsi_hba_cmd_data_in)(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                                     uint8_t cbCDB, uint8_t __far *buffer, uint32_t length);

typedef struct
{
    uint16_t              idPciVendor;
    uint16_t              idPciDevice;
    scsi_hba_detect       detect;
    scsi_hba_init         init;
    scsi_hba_cmd_data_out cmd_data_out;
    scsi_hba_cmd_data_in  cmd_data_in;
} scsi_hba_t;

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

void inline high_bits_save(uint16_t __far *pu16EaxHi)
{
    *pu16EaxHi = eax_hi_rd();
}

void inline high_bits_restore(uint16_t u16EaxHi)
{
    eax_hi_wr(u16EaxHi);
}

/* Pointers to the HBA specific access routines. */
scsi_hba_t hbaacc[] =
{
    { 0x1000, 0x0030, NULL,                 lsilogic_scsi_init, lsilogic_scsi_cmd_data_out, lsilogic_scsi_cmd_data_in }, /* SPI */
    { 0x1000, 0x0054, NULL,                 lsilogic_scsi_init, lsilogic_scsi_cmd_data_out, lsilogic_scsi_cmd_data_in }, /* SAS */
    { 0x104b, 0x1040, NULL,                 buslogic_scsi_init, buslogic_scsi_cmd_data_out, buslogic_scsi_cmd_data_in }, /* PCI */
#ifdef VBOX_WITH_VIRTIO_SCSI
    { 0x1af4, 0x1048, NULL,                 virtio_scsi_init,   virtio_scsi_cmd_data_out,   virtio_scsi_cmd_data_in   },
#endif
    { 0xffff, 0xffff, btaha_scsi_detect,    btaha_scsi_init,    buslogic_scsi_cmd_data_out, buslogic_scsi_cmd_data_in }  /* ISA */
};

/**
 * Read sectors from an attached SCSI device.
 *
 * @returns status code.
 * @param   bios_dsk    Pointer to disk request packet (in the
 *                      EBDA).
 */
int scsi_read_sectors(bio_dsk_t __far *bios_dsk)
{
    uint8_t             rc;
    cdb_rw16            cdb;
    uint32_t            count;
    uint16_t            hba_seg;
    uint8_t             idx_hba;
    uint8_t             target_id;
    uint8_t             device_id;
    uint16_t            eax_hi;

    device_id = VBOX_GET_SCSI_DEVICE(bios_dsk->drqp.dev_id);
    if (device_id > BX_MAX_SCSI_DEVICES)
        BX_PANIC("%s: device_id out of range %d\n", __func__, device_id);

    count    = bios_dsk->drqp.nsect;

    high_bits_save(&eax_hi);

    /* Prepare a CDB. */
    cdb.command = SCSI_READ_16;
    cdb.lba     = swap_64(bios_dsk->drqp.lba);
    cdb.pad1    = 0;
    cdb.nsect32 = swap_32(count);
    cdb.pad2    = 0;


    hba_seg   = read_word(0x0040, 0x000E) + bios_dsk->scsidev[device_id].hba_ofs;
    idx_hba   = bios_dsk->scsidev[device_id].idx_hba;
    target_id = bios_dsk->scsidev[device_id].target_id;

    DBG_SCSI("%s: reading %u sectors, device %d, target %d\n", __func__,
             count, device_id, bios_dsk->scsidev[device_id].target_id);

    rc = hbaacc[idx_hba].cmd_data_in(hba_seg :> 0, target_id, (void __far *)&cdb, 16,
                                     bios_dsk->drqp.buffer, (count * 512L));
    if (!rc)
    {
        bios_dsk->drqp.trsfsectors = count;
        bios_dsk->drqp.trsfbytes   = count * 512L;
    }
    DBG_SCSI("%s: transferred %u sectors\n", __func__, bios_dsk->drqp.nsect);
    high_bits_restore(eax_hi);

    return rc;
}

/**
 * Write sectors to an attached SCSI device.
 *
 * @returns status code.
 * @param   bios_dsk    Pointer to disk request packet (in the
 *                      EBDA).
 */
int scsi_write_sectors(bio_dsk_t __far *bios_dsk)
{
    uint8_t             rc;
    cdb_rw16            cdb;
    uint32_t            count;
    uint16_t            hba_seg;
    uint8_t             idx_hba;
    uint8_t             target_id;
    uint8_t             device_id;
    uint16_t            eax_hi;

    device_id = VBOX_GET_SCSI_DEVICE(bios_dsk->drqp.dev_id);
    if (device_id > BX_MAX_SCSI_DEVICES)
        BX_PANIC("%s: device_id out of range %d\n", __func__, device_id);

    count    = bios_dsk->drqp.nsect;

    high_bits_save(&eax_hi);

    /* Prepare a CDB. */
    cdb.command = SCSI_WRITE_16;
    cdb.lba     = swap_64(bios_dsk->drqp.lba);
    cdb.pad1    = 0;
    cdb.nsect32 = swap_32(count);
    cdb.pad2    = 0;

    hba_seg   = read_word(0x0040, 0x000E) + bios_dsk->scsidev[device_id].hba_ofs;
    idx_hba   = bios_dsk->scsidev[device_id].idx_hba;
    target_id = bios_dsk->scsidev[device_id].target_id;

    DBG_SCSI("%s: writing %u sectors, device %d, target %d\n", __func__,
             count, device_id, bios_dsk->scsidev[device_id].target_id);

    rc = hbaacc[idx_hba].cmd_data_out(hba_seg :> 0, target_id, (void __far *)&cdb, 16,
                                      bios_dsk->drqp.buffer, (count * 512L));
    if (!rc)
    {
        bios_dsk->drqp.trsfsectors = count;
        bios_dsk->drqp.trsfbytes   = (count * 512L);
    }
    DBG_SCSI("%s: transferred %u sectors\n", __func__, bios_dsk->drqp.nsect);
    high_bits_restore(eax_hi);

    return rc;
}


/// @todo move
#define ATA_DATA_NO      0x00
#define ATA_DATA_IN      0x01
#define ATA_DATA_OUT     0x02

/**
 * Perform a "packet style" read with supplied CDB.
 *
 * @returns status code.
 * @param   device_id   ID of the device to access.
 * @param   cmdlen      Length of the CDB.
 * @param   cmdbuf      The CDB buffer.
 * @param   length      How much to transfer.
 * @param   inout       Read/Write direction indicator.
 * @param   buffer      Data buffer to store the data from the device in.
 */
uint16_t scsi_cmd_packet(uint16_t device_id, uint8_t cmdlen, char __far *cmdbuf,
                         uint32_t length, uint8_t inout, char __far *buffer)
{
    bio_dsk_t __far *bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;
    uint8_t         rc;
    uint8_t         target_id;
    uint16_t        hba_seg;
    uint8_t         idx_hba;
    uint16_t        eax_hi;

    /* Data out is currently not supported. */
    if (inout == ATA_DATA_OUT) {
        BX_INFO("%s: DATA_OUT not supported yet\n", __func__);
        return 1;
    }

    /* Convert to SCSI specific device number. */
    device_id = VBOX_GET_SCSI_DEVICE(device_id);

    DBG_SCSI("%s: reading %lu bytes, device %d, target %d\n", __func__,
             length, device_id, bios_dsk->scsidev[device_id].target_id);
    DBG_SCSI("%s: reading %u %u-byte sectors\n", __func__,
             bios_dsk->drqp.nsect, bios_dsk->drqp.sect_sz);

    high_bits_save(&eax_hi);
    hba_seg   = read_word(0x0040, 0x000E) + bios_dsk->scsidev[device_id].hba_ofs;
    idx_hba   = bios_dsk->scsidev[device_id].idx_hba;
    target_id = bios_dsk->scsidev[device_id].target_id;

    bios_dsk->drqp.lba     = length << 8;     /// @todo xfer length limit
    bios_dsk->drqp.buffer  = buffer;
    bios_dsk->drqp.nsect   = length / bios_dsk->drqp.sect_sz;

    DBG_SCSI("%s: reading %u bytes, device %d, target %d\n", __func__,
             length, device_id, bios_dsk->scsidev[device_id].target_id);

    rc = hbaacc[idx_hba].cmd_data_in(hba_seg :> 0, target_id, (void __far *)cmdbuf, cmdlen,
                                     bios_dsk->drqp.buffer, length);
    if (!rc)
        bios_dsk->drqp.trsfbytes = length;

    DBG_SCSI("%s: transferred %u bytes\n", __func__, length);
    high_bits_restore(eax_hi);

    return rc;
}

/**
 * Enumerate attached devices.
 *
 * @param   hba_seg    Segement of the HBA controller block.
 * @param   hba_ofs    Offset of the HBA controller block within the EBDA.
 * @param   idx_hba    The HBA driver index used for accessing the enumerated devices.
 */
static void scsi_enumerate_attached_devices(uint16_t hba_seg, uint16_t hba_ofs, uint8_t idx_hba)
{
    int                 i;
    uint8_t             buffer[0x0200];
    bio_dsk_t __far     *bios_dsk;

    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;

    /* Go through target devices. */
    for (i = 0; i < VBSCSI_MAX_DEVICES; i++)
    {
        uint8_t     rc;
        uint8_t     aCDB[16];
        uint8_t     hd_index, devcount_scsi;

        aCDB[0] = SCSI_INQUIRY;
        aCDB[1] = 0;
        aCDB[2] = 0;
        aCDB[3] = 0;
        aCDB[4] = 5; /* Allocation length. */
        aCDB[5] = 0;

        rc = hbaacc[idx_hba].cmd_data_in(hba_seg :> 0, i, aCDB, 6, buffer, 5);
        if (rc != 0)
        {
            DBG_SCSI("%s: SCSI_INQUIRY failed\n", __func__); /* Not a fatal error if the device doesn't exist. */
            continue;
        }

        devcount_scsi = bios_dsk->scsi_devcount;

        /* Check the attached device. */
        if (   ((buffer[0] & 0xe0) == 0)
            && ((buffer[0] & 0x1f) == 0x00))
        {
            DBG_SCSI("%s: Disk detected at %d\n", __func__, i);

            /* We add the disk only if the maximum is not reached yet. */
            if (devcount_scsi < BX_MAX_SCSI_DEVICES)
            {
                uint64_t    sectors, t;
                uint32_t    sector_size, cylinders;
                uint16_t    heads, sectors_per_track;
                uint8_t     hdcount;
                uint8_t     cmos_base;

                /* Issue a read capacity command now. */
                _fmemset(aCDB, 0, sizeof(aCDB));
                aCDB[0] = SCSI_SERVICE_ACT;
                aCDB[1] = SCSI_READ_CAP_16;
                aCDB[13] = 32; /* Allocation length. */

                rc = hbaacc[idx_hba].cmd_data_in(hba_seg :> 0, i, aCDB, 16, buffer, 32);
                if (rc != 0)
                    BX_PANIC("%s: SCSI_READ_CAPACITY failed\n", __func__);

                /* The value returned is the last addressable LBA, not
                 * the size, which what "+ 1" is for.
                 */
                sectors = swap_64(*(uint64_t *)buffer) + 1;

                sector_size =   ((uint32_t)buffer[8] << 24)
                              | ((uint32_t)buffer[9] << 16)
                              | ((uint32_t)buffer[10] << 8)
                              | ((uint32_t)buffer[11]);

                /* We only support the disk if sector size is 512 bytes. */
                if (sector_size != 512)
                {
                    /* Leave a log entry. */
                    BX_INFO("Disk %d has an unsupported sector size of %u\n", i, sector_size);
                    continue;
                }

                /* Get logical CHS geometry. */
                switch (devcount_scsi)
                {
                    case 0:
                        cmos_base = 0x90;
                        break;
                    case 1:
                        cmos_base = 0x98;
                        break;
                    case 2:
                        cmos_base = 0xA0;
                        break;
                    case 3:
                        cmos_base = 0xA8;
                        break;
                    default:
                        cmos_base = 0;
                }

                if (cmos_base && inb_cmos(cmos_base + 7))
                {
                    /* If provided, grab the logical geometry from CMOS. */
                    cylinders         = get_cmos_word(cmos_base /*, cmos_base + 1*/);
                    heads             = inb_cmos(cmos_base + 2);
                    sectors_per_track = inb_cmos(cmos_base + 7);
                }
                else
                {
                    /* Calculate default logical geometry. NB: Very different
                     * from default ATA/SATA logical geometry!
                     */
                    if (sectors >= (uint32_t)4 * 1024 * 1024)
                    {
                        heads = 255;
                        sectors_per_track = 63;
                        /* Approximate x / (255 * 63) using shifts */
                        t = (sectors >> 6) + (sectors >> 12);
                        cylinders = (t >> 8) + (t >> 16);
                    }
                    else if (sectors >= (uint32_t)2 * 1024 * 1024)
                    {
                        heads = 128;
                        sectors_per_track = 32;
                        cylinders = sectors >> 12;
                    }
                    else
                    {
                        heads = 64;
                        sectors_per_track = 32;
                        cylinders = sectors >> 11;
                    }
                }

                /* Calculate index into the generic disk table. */
                hd_index = devcount_scsi + BX_MAX_ATA_DEVICES;

                bios_dsk->scsidev[devcount_scsi].hba_ofs   = hba_ofs;
                bios_dsk->scsidev[devcount_scsi].idx_hba   = idx_hba;
                bios_dsk->scsidev[devcount_scsi].target_id = i;
                bios_dsk->devices[hd_index].type        = DSK_TYPE_SCSI;
                bios_dsk->devices[hd_index].device      = DSK_DEVICE_HD;
                bios_dsk->devices[hd_index].removable   = 0;
                bios_dsk->devices[hd_index].lock        = 0;
                bios_dsk->devices[hd_index].blksize     = sector_size;
                bios_dsk->devices[hd_index].translation = GEO_TRANSLATION_LBA;

                /* Write LCHS/PCHS values. */
                bios_dsk->devices[hd_index].lchs.heads = heads;
                bios_dsk->devices[hd_index].lchs.spt   = sectors_per_track;
                bios_dsk->devices[hd_index].pchs.heads = heads;
                bios_dsk->devices[hd_index].pchs.spt   = sectors_per_track;

                if (cylinders > 1024) {
                    bios_dsk->devices[hd_index].lchs.cylinders = 1024;
                    bios_dsk->devices[hd_index].pchs.cylinders = 1024;
                } else {
                    bios_dsk->devices[hd_index].lchs.cylinders = (uint16_t)cylinders;
                    bios_dsk->devices[hd_index].pchs.cylinders = (uint16_t)cylinders;
                }

                BX_INFO("SCSI %d-ID#%d: LCHS=%lu/%u/%u 0x%llx sectors\n", devcount_scsi,
                        i, (uint32_t)cylinders, heads, sectors_per_track, sectors);

                bios_dsk->devices[hd_index].sectors = sectors;

                /* Store the id of the disk in the ata hdidmap. */
                hdcount = bios_dsk->hdcount;
                bios_dsk->hdidmap[hdcount] = devcount_scsi + BX_MAX_ATA_DEVICES;
                hdcount++;
                bios_dsk->hdcount = hdcount;

                /* Update hdcount in the BDA. */
                hdcount = read_byte(0x40, 0x75);
                hdcount++;
                write_byte(0x40, 0x75, hdcount);

                devcount_scsi++;
            }
            else
            {
                /* We reached the maximum of SCSI disks we can boot from. We can quit detecting. */
                break;
            }
        }
        else if (   ((buffer[0] & 0xe0) == 0)
                 && ((buffer[0] & 0x1f) == 0x05))
        {
            uint8_t     cdcount;
            uint8_t     removable;

            BX_INFO("SCSI %d-ID#%d: CD/DVD-ROM\n", devcount_scsi, i);

            /* Calculate index into the generic device table. */
            hd_index = devcount_scsi + BX_MAX_ATA_DEVICES;

            removable = buffer[1] & 0x80 ? 1 : 0;

            bios_dsk->scsidev[devcount_scsi].hba_ofs   = hba_ofs;
            bios_dsk->scsidev[devcount_scsi].idx_hba   = idx_hba;
            bios_dsk->scsidev[devcount_scsi].target_id = i;
            bios_dsk->devices[hd_index].type        = DSK_TYPE_SCSI;
            bios_dsk->devices[hd_index].device      = DSK_DEVICE_CDROM;
            bios_dsk->devices[hd_index].removable   = removable;
            bios_dsk->devices[hd_index].blksize     = 2048;
            bios_dsk->devices[hd_index].translation = GEO_TRANSLATION_NONE;

            /* Store the ID of the device in the BIOS cdidmap. */
            cdcount = bios_dsk->cdcount;
            bios_dsk->cdidmap[cdcount] = devcount_scsi + BX_MAX_ATA_DEVICES;
            cdcount++;
            bios_dsk->cdcount = cdcount;

            devcount_scsi++;
        }
        else
            DBG_SCSI("%s: No supported device detected at %d\n", __func__, i);

        bios_dsk->scsi_devcount = devcount_scsi;
    }
}

/**
 * Init the SCSI driver and detect attached disks.
 */
void BIOSCALL scsi_init(void)
{
    int i;
    bio_dsk_t __far     *bios_dsk;

    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;
    bios_dsk->scsi_devcount = 0;

    /* Walk the supported drivers and try to detect the HBA. */
    for (i = 0; i < sizeof(hbaacc)/sizeof(hbaacc[0]); i++)
    {
        uint16_t busdevfn;

        if (hbaacc[i].detect) {
            busdevfn = hbaacc[i].detect();
        } else {
            busdevfn = pci_find_device(hbaacc[i].idPciVendor, hbaacc[i].idPciDevice);
        }

        if (busdevfn != VBOX_SCSI_NO_HBA)
        {
            int rc;
            uint8_t  u8Bus, u8DevFn;
            uint16_t hba_seg;
            uint16_t hba_ofs = conv_mem_alloc(1/*KB*/, 1/*in_ebda*/);
            if (hba_ofs == 0) /* No point in trying the rest if we are out of memory. */
                break;

            hba_seg = read_word(0x0040, 0x000E) + hba_ofs;

            u8Bus = (busdevfn & 0xff00) >> 8;
            u8DevFn = busdevfn & 0x00ff;

            DBG_SCSI("SCSI HBA at Bus %u DevFn 0x%x (raw 0x%x)\n", u8Bus, u8DevFn, busdevfn);
            rc = hbaacc[i].init(hba_seg :> 0, u8Bus, u8DevFn);
            if (!rc)
                scsi_enumerate_attached_devices(hba_seg, hba_ofs, i);
            /** @todo Free memory on error. */
        }
    }
}
