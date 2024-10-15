/* $Id: vds.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Utility routines for calling the Virtual DMA Services.
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
#include "biosint.h"
#include "vds.h"

typedef struct {
    uint8_t     major;      /* VDS spec major version number. */
    uint8_t     minor;      /* VDS spec minor version number. */
    uint16_t    flags;      /* Capabilities/status flags. */
    uint16_t    prod_no;    /* Product number. */
    uint16_t    prod_rev;   /* Product revision number. */
    uint32_t    max_buf;    /* Maximum buffer size supported. */
} vds_ver;

int vds_is_present( void )
{
    uint8_t __far   *vds_flags;

    vds_flags = MK_FP( 0x40, VDS_FLAGS_OFS );
    return( !!(*vds_flags & VDS_PRESENT) );
}

int vds_lock_sg( vds_edds __far *edds );
#pragma aux vds_lock_sg =   \
    "mov    ax, 8105h"      \
    "mov    dx, 0"          \
    "int    4Bh"            \
    "jc     error"          \
    "xor    al, al"         \
    "error:"                \
    "cbw"                   \
    parm [es di] value [ax] modify [dx];

int vds_unlock_sg( vds_edds __far *edds );
#pragma aux vds_unlock_sg = \
    "mov    ax, 8106h"      \
    "mov    dx, 0"          \
    "int    4Bh"            \
    "jc     error"          \
    "xor    al, al"         \
    "error:"                \
    "cbw"                   \
    parm [es di] value [ax] modify [dx];


/*
 * Convert a real mode 16:16 segmented address to a simple 32-bit
 * linear address.
 */
uint32_t vds_real_to_lin( void __far *ptr )
{
    return( ((uint32_t)FP_SEG( ptr ) << 4) + FP_OFF( ptr ) );
}

/*
 * Build a VDS-style scatter/gather list, regardless of whether VDS is
 * present or not. This routine either calls VDS to do the work or
 * trivially creates the list if no remapping is needed.
 */
int vds_build_sg_list( vds_edds __far *edds, void __far *buf, uint32_t len )
{
    int     rc;

    /* NB: The num_avail field in the EDDS must be set correctly! */
    edds->region_size = len;
    edds->offset = vds_real_to_lin( buf );
    edds->seg_sel = 0;  /* Indicates a linear address. */
    if( vds_is_present() ) {
        /* VDS is present, use it. */
        rc = vds_lock_sg( edds );
    } else {
        /* No VDS, do it ourselves with one S/G entry. */
        edds->num_used = 1;
        edds->u.sg[0].phys_addr = edds->offset;
        edds->u.sg[0].size      = len;
        rc = VDS_SUCCESS;
    }
    return( rc );
}

/*
 * Free a VDS-style scatter/gather list, regardless of whether VDS
 * is present or not.
 */
int vds_free_sg_list( vds_edds __far *edds )
{
    int     rc;

    if( vds_is_present() ) {
        /* VDS is present, use it. */
        rc = vds_unlock_sg( edds );
    } else {
        /* No VDS, not much to do. */
        /* We could check here if the EDDS had in fact been built by us.
         * But if VDS really went away, what can we do about it anyway?
         */
        rc = VDS_SUCCESS;
    }
    edds->num_used = 0;
    return( rc );
}
