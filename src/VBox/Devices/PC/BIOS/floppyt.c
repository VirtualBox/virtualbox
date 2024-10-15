/* $Id: floppyt.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Floppy drive tables.
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
#include "inlines.h"

/**
 * Extended DPT (Disk Parameter Table) structure.
 */
typedef struct
{
    uint8_t     spec1;      /* First SPECIFY byte. */
    uint8_t     spec2;      /* Second SPECIFY byte. */
    uint8_t     mot_wait;   /* Motor wait time after operation. */
    uint8_t     ss_code;    /* Sector size code. */
    uint8_t     eot;        /* End of Track (ID of last sector). */
    uint8_t     gap;        /* Gap length. */
    uint8_t     dtl;        /* Data length. */
    uint8_t     fmt_gap;    /* Gap length for format. */
    uint8_t     fmt_fill;   /* Format fill byte. */
    uint8_t     hd_settle;  /* Head settle time (msec). */
    uint8_t     mot_start;  /* Motor start time (1/8 sec units). */
    uint8_t     max_trk;    /* Maximum track number. */
    uint8_t     rate;       /* Data transfer rate code. */
} dpt_ext;

ct_assert(sizeof(dpt_ext) == 13);

/* Motor spin-up wait time in BIOS ticks (~2 seconds). */
#define MOTOR_WAIT  0x25

/* Data rates as stored in the DPT */
#define RATE_250K   0x80
#define RATE_300K   0x40
#define RATE_500K   0x00
#define RATE_1M     0xC0

/* In the 13-entry DPT, 7 entries are constant. Use a macro to set those. */
#define MAKE_DPT_ENTRY(sp1, eot, gap, fgp, mxt, dtr)    \
        { sp1, 2, MOTOR_WAIT, 2, eot, gap, 0xFF, fgp, 0xF6, 15, 8, mxt, dtr }

dpt_ext fd_parm[] = {
    MAKE_DPT_ENTRY(0xDF,   9, 0x2A, 0x50,  39, RATE_250K),  /* 360K disk/360K drive */
    MAKE_DPT_ENTRY(0xDF,   9, 0x2A, 0x50,  39, RATE_300K),  /* 360K disk/1.2M drive */
    MAKE_DPT_ENTRY(0xDF,  15, 0x1B, 0x54,  79, RATE_500K),  /* 1.2M disk */
    MAKE_DPT_ENTRY(0xDF,   9, 0x2A, 0x50,  79, RATE_250K),  /* 720K disk */
    MAKE_DPT_ENTRY(0xAF,  18, 0x1B, 0x6C,  79, RATE_500K),  /* 1.44M disk */
    MAKE_DPT_ENTRY(0xAF,  36, 0x1B, 0x54,  79, RATE_1M),    /* 2.88M disk */
    MAKE_DPT_ENTRY(0xAF, 255, 0x1B, 0x54, 255, RATE_500K)   /* Fake mega-disk */
};

typedef struct {
    uint8_t     type;       /* Drive type. */
    uint8_t     dpt_entry;  /* Index of entry in fd_parm. */
} fd_map_entry;

/* Drive types as stored in the CMOS. Must match DevPCBios! */
#define FDRV_360K   1
#define FDRV_1_2M   2
#define FDRV_720K   3
#define FDRV_1_44M  4
#define FDRV_2_88M  5
#define FDRV_15M    14
#define FDRV_63M    15

/* A table mapping (CMOS) drive types to DPT entries. */
fd_map_entry    fd_map[] = {
    { FDRV_360K,  0 },
    { FDRV_1_2M,  2 },
    { FDRV_720K,  3 },
    { FDRV_1_44M, 4 },
    { FDRV_2_88M, 5 },
    { FDRV_15M,   6 },
    { FDRV_63M,   6 }
};

/* Find a DPT corresponding to the given drive type. */
dpt_ext *get_floppy_dpt(uint8_t drv_typ)
{
    int     i;

    for (i = 0; i < sizeof(fd_map) / sizeof(fd_map[0]); ++i)
        if (fd_map[i].type == drv_typ)
            return &fd_parm[fd_map[i].dpt_entry];

    /* As a fallback, return the 1.44M DPT. */
    return &fd_parm[5];
}
