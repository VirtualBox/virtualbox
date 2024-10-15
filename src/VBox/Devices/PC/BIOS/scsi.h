/* $Id: scsi.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PC BIOS - SCSI definitions.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_PC_BIOS_scsi_h
#define VBOX_INCLUDED_SRC_PC_BIOS_scsi_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Command opcodes. */
#define SCSI_SERVICE_ACT   0x9e
#define SCSI_INQUIRY       0x12
#define SCSI_READ_CAP_10   0x25
#define SCSI_READ_10       0x28
#define SCSI_WRITE_10      0x2a
#define SCSI_READ_CAP_16   0x10    /* Not an opcode by itself, sub-action for the "Service Action" */
#define SCSI_READ_16       0x88
#define SCSI_WRITE_16      0x8a

#pragma pack(1)

/* READ_10/WRITE_10 CDB layout. */
typedef struct {
    uint16_t    command;    /* Command. */
    uint32_t    lba;        /* LBA, MSB first! */
    uint8_t     pad1;       /* Unused. */
    uint16_t    nsect;      /* Sector count, MSB first! */
    uint8_t     pad2;       /* Unused. */
} cdb_rw10;

/* READ_16/WRITE_16 CDB layout. */
typedef struct {
    uint16_t    command;    /* Command. */
    uint64_t    lba;        /* LBA, MSB first! */
    uint32_t    nsect32;    /* Sector count, MSB first! */
    uint8_t     pad1;       /* Unused. */
    uint8_t     pad2;       /* Unused. */
} cdb_rw16;

#pragma pack()

ct_assert(sizeof(cdb_rw10) == 10);
ct_assert(sizeof(cdb_rw16) == 16);

extern int lsilogic_scsi_init(void __far *pvHba, uint8_t u8Bus, uint8_t u8DevFn);
extern int lsilogic_scsi_cmd_data_out(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                                      uint8_t cbCDB, uint8_t __far *buffer, uint32_t length);
extern int lsilogic_scsi_cmd_data_in(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                                     uint8_t cbCDB, uint8_t __far *buffer, uint32_t length);

extern int buslogic_scsi_init(void __far *pvHba, uint8_t u8Bus, uint8_t u8DevFn);
extern int buslogic_scsi_cmd_data_out(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                                      uint8_t cbCDB, uint8_t __far *buffer, uint32_t length);
extern int buslogic_scsi_cmd_data_in(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                                     uint8_t cbCDB, uint8_t __far *buffer, uint32_t length);

extern uint16_t btaha_scsi_detect();
extern int btaha_scsi_init(void __far *pvHba, uint8_t u8Bus, uint8_t u8DevFn);

extern int virtio_scsi_init(void __far *pvHba, uint8_t u8Bus, uint8_t u8DevFn);
extern int virtio_scsi_cmd_data_out(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                                    uint8_t cbCDB, uint8_t __far *buffer, uint32_t length);
extern int virtio_scsi_cmd_data_in(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                                   uint8_t cbCDB, uint8_t __far *buffer, uint32_t length);

#endif /* !VBOX_INCLUDED_SRC_PC_BIOS_scsi_h */

