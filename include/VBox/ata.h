/* $Id: ata.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: ATA/ATAPI declarations
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_ata_h
#define VBOX_INCLUDED_ata_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/* Bits of HD_STATUS */
#define ATA_STAT_ERR                0x01
#define ATA_STAT_INDEX              0x02
#define ATA_STAT_ECC                0x04    /* Corrected error */
#define ATA_STAT_DRQ                0x08
#define ATA_STAT_SEEK               0x10
#define ATA_STAT_SRV                0x10
#define ATA_STAT_WRERR              0x20
#define ATA_STAT_READY              0x40
#define ATA_STAT_BUSY               0x80

/* Bits for HD_ERROR */
#define MARK_ERR                0x01    /* Bad address mark */
#define TRK0_ERR                0x02    /* couldn't find track 0 */
#define ABRT_ERR                0x04    /* Command aborted */
#define MCR_ERR                 0x08    /* media change request */
#define ID_ERR                  0x10    /* ID field not found */
#define MC_ERR                  0x20    /* media changed */
#define ECC_ERR                 0x40    /* Uncorrectable ECC error */
#define BBD_ERR                 0x80    /* pre-EIDE meaning:  block marked bad */
#define ICRC_ERR                0x80    /* new meaning:  CRC error during transfer */

/* Bits for uATARegDevCtl. */
#define ATA_DEVCTL_DISABLE_IRQ  0x02
#define ATA_DEVCTL_RESET        0x04
#define ATA_DEVCTL_HOB          0x80


/* ATA/ATAPI Commands (as of ATA/ATAPI-8 draft T13/1699D Revision 3a).
 * Please keep this in sync with g_apszATACmdNames. */
typedef enum ATACMD
{
    ATA_NOP                                 = 0x00,
    ATA_CFA_REQUEST_EXTENDED_ERROR_CODE     = 0x03,
    ATA_DATA_SET_MANAGEMENT                 = 0x06,
    ATA_DEVICE_RESET                        = 0x08,
    ATA_RECALIBRATE                         = 0x10,
    ATA_READ_SECTORS                        = 0x20,
    ATA_READ_SECTORS_WITHOUT_RETRIES        = 0x21,
    ATA_READ_LONG                           = 0x22,
    ATA_READ_LONG_WITHOUT_RETRIES           = 0x23,
    ATA_READ_SECTORS_EXT                    = 0x24,
    ATA_READ_DMA_EXT                        = 0x25,
    ATA_READ_DMA_QUEUED_EXT                 = 0x26,
    ATA_READ_NATIVE_MAX_ADDRESS_EXT         = 0x27,
    ATA_READ_MULTIPLE_EXT                   = 0x29,
    ATA_READ_STREAM_DMA_EXT                 = 0x2a,
    ATA_READ_STREAM_EXT                     = 0x2b,
    ATA_READ_LOG_EXT                        = 0x2f,
    ATA_WRITE_SECTORS                       = 0x30,
    ATA_WRITE_SECTORS_WITHOUT_RETRIES       = 0x31,
    ATA_WRITE_LONG                          = 0x32,
    ATA_WRITE_LONG_WITHOUT_RETRIES          = 0x33,
    ATA_WRITE_SECTORS_EXT                   = 0x34,
    ATA_WRITE_DMA_EXT                       = 0x35,
    ATA_WRITE_DMA_QUEUED_EXT                = 0x36,
    ATA_SET_MAX_ADDRESS_EXT                 = 0x37,
    ATA_CFA_WRITE_SECTORS_WITHOUT_ERASE     = 0x38,
    ATA_WRITE_MULTIPLE_EXT                  = 0x39,
    ATA_WRITE_STREAM_DMA_EXT                = 0x3a,
    ATA_WRITE_STREAM_EXT                    = 0x3b,
    ATA_WRITE_VERIFY                        = 0x3c,
    ATA_WRITE_DMA_FUA_EXT                   = 0x3d,
    ATA_WRITE_DMA_QUEUED_FUA_EXT            = 0x3e,
    ATA_WRITE_LOG_EXT                       = 0x3f,
    ATA_READ_VERIFY_SECTORS                 = 0x40,
    ATA_READ_VERIFY_SECTORS_WITHOUT_RETRIES = 0x41,
    ATA_READ_VERIFY_SECTORS_EXT             = 0x42,
    ATA_WRITE_UNCORRECTABLE_EXT             = 0x45,
    ATA_READ_LOG_DMA_EXT                    = 0x47,
    ATA_FORMAT_TRACK                        = 0x50,
    ATA_CONFIGURE_STREAM                    = 0x51,
    ATA_WRITE_LOG_DMA_EXT                   = 0x57,
    ATA_TRUSTED_RECEIVE                     = 0x5c,
    ATA_TRUSTED_RECEIVE_DMA                 = 0x5d,
    ATA_TRUSTED_SEND                        = 0x5e,
    ATA_TRUSTED_SEND_DMA                    = 0x5f,
    ATA_READ_FPDMA_QUEUED                   = 0x60,
    ATA_WRITE_FPDMA_QUEUED                  = 0x61,
    ATA_SEEK                                = 0x70,
    ATA_CFA_TRANSLATE_SECTOR                = 0x87,
    ATA_EXECUTE_DEVICE_DIAGNOSTIC           = 0x90,
    ATA_INITIALIZE_DEVICE_PARAMETERS        = 0x91,
    ATA_DOWNLOAD_MICROCODE                  = 0x92,
    ATA_STANDBY_IMMEDIATE__ALT              = 0x94,
    ATA_IDLE_IMMEDIATE__ALT                 = 0x95,
    ATA_STANDBY__ALT                        = 0x96,
    ATA_IDLE__ALT                           = 0x97,
    ATA_CHECK_POWER_MODE__ALT               = 0x98,
    ATA_SLEEP__ALT                          = 0x99,
    ATA_PACKET                              = 0xa0,
    ATA_IDENTIFY_PACKET_DEVICE              = 0xa1,
    ATA_SERVICE                             = 0xa2,
    ATA_SMART                               = 0xb0,
    ATA_DEVICE_CONFIGURATION_OVERLAY        = 0xb1,
    ATA_NV_CACHE                            = 0xb6,
    ATA_CFA_ERASE_SECTORS                   = 0xc0,
    ATA_READ_MULTIPLE                       = 0xc4,
    ATA_WRITE_MULTIPLE                      = 0xc5,
    ATA_SET_MULTIPLE_MODE                   = 0xc6,
    ATA_READ_DMA_QUEUED                     = 0xc7,
    ATA_READ_DMA                            = 0xc8,
    ATA_READ_DMA_WITHOUT_RETRIES            = 0xc9,
    ATA_WRITE_DMA                           = 0xca,
    ATA_WRITE_DMA_WITHOUT_RETRIES           = 0xcb,
    ATA_WRITE_DMA_QUEUED                    = 0xcc,
    ATA_CFA_WRITE_MULTIPLE_WITHOUT_ERASE    = 0xcd,
    ATA_WRITE_MULTIPLE_FUA_EXT              = 0xce,
    ATA_CHECK_MEDIA_CARD_TYPE               = 0xd1,
    ATA_GET_MEDIA_STATUS                    = 0xda,
    ATA_ACKNOWLEDGE_MEDIA_CHANGE            = 0xdb,
    ATA_BOOT_POST_BOOT                      = 0xdc,
    ATA_BOOT_PRE_BOOT                       = 0xdd,
    ATA_MEDIA_LOCK                          = 0xde,
    ATA_MEDIA_UNLOCK                        = 0xdf,
    ATA_STANDBY_IMMEDIATE                   = 0xe0,
    ATA_IDLE_IMMEDIATE                      = 0xe1,
    ATA_STANDBY                             = 0xe2,
    ATA_IDLE                                = 0xe3,
    ATA_READ_BUFFER                         = 0xe4,
    ATA_CHECK_POWER_MODE                    = 0xe5,
    ATA_SLEEP                               = 0xe6,
    ATA_FLUSH_CACHE                         = 0xe7,
    ATA_WRITE_BUFFER                        = 0xe8,
    ATA_WRITE_SAME                          = 0xe9,
    ATA_FLUSH_CACHE_EXT                     = 0xea,
    ATA_IDENTIFY_DEVICE                     = 0xec,
    ATA_MEDIA_EJECT                         = 0xed,
    ATA_IDENTIFY_DMA                        = 0xee,
    ATA_SET_FEATURES                        = 0xef,
    ATA_SECURITY_SET_PASSWORD               = 0xf1,
    ATA_SECURITY_UNLOCK                     = 0xf2,
    ATA_SECURITY_ERASE_PREPARE              = 0xf3,
    ATA_SECURITY_ERASE_UNIT                 = 0xf4,
    ATA_SECURITY_FREEZE_LOCK                = 0xf5,
    ATA_SECURITY_DISABLE_PASSWORD           = 0xf6,
    ATA_READ_NATIVE_MAX_ADDRESS             = 0xf8,
    ATA_SET_MAX                             = 0xf9
} ATACMD;


#define ATA_MODE_MDMA   0x20
#define ATA_MODE_UDMA   0x40


#define ATA_TRANSFER_ID(thismode, maxspeed, currmode) \
    (   ((1 << (maxspeed + 1)) - 1) \
     |  ((((thismode ^ currmode) & 0xf8) == 0) ? 1 << ((currmode & 0x07) + 8) : 0))

/**
 * Length of the ATA VPD data (without termination)
 */
#define ATA_SERIAL_NUMBER_LENGTH        20
#define ATA_FIRMWARE_REVISION_LENGTH     8
#define ATA_MODEL_NUMBER_LENGTH         40

/** Mask to get the LBA value from a LBA range. */
#define ATA_RANGE_LBA_MASK    UINT64_C(0xffffffffffff)
/** Mas to get the length value from a LBA range. */
#define ATA_RANGE_LENGTH_MASK UINT64_C(0xffff000000000000)
/** Returns the length of the range in sectors. */
#define ATA_RANGE_LENGTH_GET(val) (((val) & ATA_RANGE_LENGTH_MASK) >> 48)

/* ATAPI defines */

#define ATAPI_PACKET_SIZE 12


#define ATAPI_INT_REASON_CD             0x01 /* 0 = data transfer */
#define ATAPI_INT_REASON_IO             0x02 /* 1 = transfer to the host */
#define ATAPI_INT_REASON_REL            0x04
#define ATAPI_INT_REASON_TAG_MASK       0xf8

#if defined(LOG_ENABLED) && defined(IN_RING3)
const char * ATACmdText(uint8_t uCmd);
#endif

#endif /* !VBOX_INCLUDED_ata_h */

